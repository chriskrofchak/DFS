// LOCAL HEADERS
#include "watdfs_client.h"
#include "watdfs_make_args.h"
#include "debug.h"
#include "rpc.h"
#include "a2_client.h"
#include "rpc_lock.h"

// SYSCALLS
#include <time.h>
#include <fcntl.h>           /* Definition of AT_* constants */
#include <sys/stat.h>

// C++ HEADERS
#include "openbook.h"
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include "watdfs_client.h"

bool watdfs_cli_file_exists(const char *path) {
    std::string full_path = absolut_path(path);
    struct stat statbuf{};
    int fn_ret = stat(full_path.c_str(), &statbuf);
    if (fn_ret < 0) {
        DLOG("in watdfs_cli_fresh_file with errno %d", -errno);
        if (errno == ENOENT) return false;
    } else {
        DLOG("in watdfs_cli_fresh_file on %s, and file exists already...", path);
    }
    // to do, freshness checks
    return true; // file exists
}

bool operator==(const timespec &l, const timespec &r) {
    return (l.tv_sec == r.tv_sec) && (l.tv_nsec == r.tv_nsec);
}

// NOW FOR A3.... HAVE THESE CHANGES
// not done when file is already open,
int transfer_file(void *userdata, const char *path, bool persist_fd, struct fuse_file_info *fi) {
    // if files open, stop

    // getattr
    struct stat statbuf{};
    int fn_ret;
    bool is_write = (fi->flags & (O_WRONLY | O_RDWR)) != 0;

    if (fi->flags & O_WRONLY) fi->flags = (fi->flags & ~O_WRONLY) | O_RDWR; // since we need to read too
    
    ////// CRITICAL SECTION
    // TODO: decide if you need to spin or not
    fn_ret = watdfs_get_rw_lock(path, is_write);

    DLOG("this is fi->flags & O_CREAT, shouldn't be 0 if O_CREAT passed: %d", fi->flags & O_CREAT);
    fn_ret = a2::watdfs_cli_open(userdata, path, fi);
    RLS_IF_ERR(fn_ret, is_write);
    HANDLE_RET("open failed in cli_transfer", fn_ret) // this error is normal if ENOFILE, ENOENT
    
    DLOG("watdfs_cli_read in transfer_file gives fh: %ld", fi->fh);

    // this way, if there was no file but O_CREAT was passed, we keep going
    fn_ret = a2::watdfs_cli_getattr(userdata, path, &statbuf);
    RLS_IF_ERR(fn_ret, is_write);
    HANDLE_RET("client stat returned error in transfer_file", fn_ret)
    
    char buf[statbuf.st_size];

    // this should be atomic
    fn_ret = a2::watdfs_cli_read(userdata, path, buf, statbuf.st_size, 0, fi);
    RLS_IF_ERR(fn_ret, is_write);
    HANDLE_RET("cli_read rpc failed in cli_transfer", fn_ret)

    
    // if we are just bringing it over to read/ not an open call,
    // we don't need to keep the fd.
    if (!persist_fd) {
        fn_ret = a2::watdfs_cli_release(userdata, path, fi); // close our FD / release
        RLS_IF_ERR(fn_ret, is_write);
        HANDLE_RET("cli_release in transfer failed", fn_ret)
    }

    fn_ret = watdfs_release_rw_lock(path, is_write);
    //// END OF CRITICAL SECTION

    // update Tc
    OpenBook *ob = static_cast<OpenBook *>(userdata);
    ob->set_validate(std::string(path), time(NULL));

    //////////
    // CLIENT WRITE

    // file may not exist on client side, so just "open" with O_CREAT
    // to create it if it doesnt exist, otherwise this does nothing
    std::string full_path = absolut_path(path);

    // DLOG("FULL_PATH STRING IS: %s", full_path.c_str());
    if (!watdfs_cli_file_exists(path)) {
        fn_ret = mknod(full_path.c_str(), statbuf.st_mode, statbuf.st_dev);
        HANDLE_SYS("client mknod failed in cli_transfer", fn_ret)
    }

    int fd = open(full_path.c_str(), O_WRONLY);
    HANDLE_SYS("client open failed in cli_transfer", fd)

    // fn_ret = close(fd);
    // HANDLE_SYS("client first close failed in transfer", fn_ret)

    DLOG("statbuf is showing a size of: %ld", statbuf.st_size);
    
    // truncate file client side
    fn_ret = ftruncate(fd, statbuf.st_size);
    HANDLE_SYS("client side truncate failed in cli_transfer", fn_ret)

    // write client side
    ssize_t b_wrote = write(fd, buf, statbuf.st_size);
    HANDLE_SYS("client write failed in cli_transfer", b_wrote)

    // update file metadata(?) TODO
    // update times? 
    struct timespec times[2] = { statbuf.st_atim, statbuf.st_mtim };
    fn_ret = futimens(fd, times);
    HANDLE_SYS("updating metadata on client file failed in cli_transfer", fn_ret)

    fn_ret = close(fd);
    HANDLE_SYS("client close failed in cli_transfer", fn_ret)
    return 0;
}

// ASSUMES THAT THE FILE IS IN THE CORRECT PATH
int watdfs_server_flush_file(void *userdata, const char *path, struct fuse_file_info *fi) {
    DLOG("IN SERVER_FLUSH_FILE");

    // get file locally
    std::string full_path = absolut_path(path);

    // read into buf
    struct stat statbuf{};
    int fn_ret = stat(full_path.c_str(), &statbuf);
    // error propagation, not necessarily bad, 
    // if file DNE then return error is correct!
    HANDLE_SYS("client stat returned error in flush_file", fn_ret)
    
    // if its in OpenBook, should close it and reopen as readonly 
    // to send to server... 
    OpenBook* ob = static_cast<OpenBook*>(userdata);
    int fd;
    if (ob->is_open(path)) {
        DLOG("FILE IS OPEN.");
        fd = ob->get_local_fd(std::string(path));
    } else {
        DLOG("FILE NOT OPEN, OPENING...");
        int fd = open(full_path.c_str(), O_RDONLY);
        HANDLE_SYS("opening file in flush_file failed", fd)
    }
    char buf[statbuf.st_size];

    DLOG("fd being used to pread in flush_file is: %d", fd);
    ssize_t b_read = pread(fd, (void *)buf, statbuf.st_size, 0);
    HANDLE_SYS("client local read failed in flush_file", b_read)
    DLOG("read into buf to flush to server: %s", buf);

    // this should be after a freshness check (if fresh, return 0)
    // else : lock and write

    // ATOMIC WRITES
    fn_ret = watdfs_get_rw_lock(path, true); // needs to write

    // write buf on server
    // todo fix fuse_file_info
    DLOG("getting ESPIPE, whats fi->fh?: %ld", fi->fh);
    fn_ret = a2::watdfs_cli_write(userdata, path, (const char *)buf, statbuf.st_size, 0, fi);

    RLS_IF_ERR(fn_ret, true);
    HANDLE_RET("write rpc failed in flush_file", fn_ret)

    // update times on server
    struct timespec times[2] = { statbuf.st_atim, statbuf.st_mtim };
    fn_ret = a2::watdfs_cli_utimensat(userdata, path, times);
    RLS_IF_ERR(fn_ret, true);
    HANDLE_RET("utimensat rpc failed in flush_file", fn_ret)

    // can release lock
    fn_ret = watdfs_release_rw_lock(path, true);

    // close on the server, not the job of flush_file
    // fn_ret = a2::watdfs_cli_release(userdata, path, fi);
    // HANDLE_RET("close rpc failed in flush_file", fn_ret)
    return 0;
}



///////
// NEWER FETCH AND FLUSH
// with clopens

int fresh_fetch(void *userdata, const char *path, struct fuse_file_info *fi) {
    // TODO 
    DLOG("IN FRESHNESS (FRESH_FETCH) CHECK CLI_READ");
    OpenBook * ob = static_cast<OpenBook*>(userdata);
    int fn_ret, fd;
    std::string full_path = absolut_path(path);

    fd = ob->get_local_fd(std::string(path));

    // transfer file
    // bool reopen = (fi->flags & (O_RDWR | O_WRONLY)) == 0;
    if (true) {
        fn_ret = close(fd);
        HANDLE_SYS("close failed in fresh_fetch", fn_ret)
        fd = open(full_path.c_str(), O_RDWR);
        HANDLE_SYS("reopen failed in fresh_fetch", fn_ret)
        // fi->fh = fd;
    }
    
    struct stat statbuf{};
    fn_ret = a2::watdfs_cli_getattr(userdata, path, &statbuf);
    HANDLE_RET("fresh a2::getattr failed in cli_read", fn_ret)
    char buf[statbuf.st_size];

    // fd_pair fdp = ob->get_fd_pair(std::string(path));
    struct fuse_file_info ser_fi{};
    ser_fi.flags = fi->flags;
    ser_fi.fh    = ob->get_server_fd(std::string(path));
    fn_ret = a2::watdfs_cli_read(userdata, path, buf, statbuf.st_size, 0, &ser_fi);
    HANDLE_RET("fresh_fetch a2::cli_read failed", fn_ret)

    fn_ret = pwrite(fd, buf, statbuf.st_size, 0);
    HANDLE_SYS("couldnt write fresh file fresh_fetch", fn_ret)
    
    if (true) {
        fn_ret = close(fd);
        HANDLE_SYS("close failed in fresh_fetch", fn_ret)
        fd = open(full_path.c_str(), fi->flags);
        HANDLE_SYS("reopen failed in fresh_fetch", fn_ret)
        // fi->fh = fd;
        ob->set_cli_fd(std::string(path), fd);
    }

    ob->set_validate(std::string(path), time(NULL));
    return 0;
}

int fresh_flush(void *userdata, const char *path, struct fuse_file_info *fi) {
    // TODO 
    DLOG("IN FRESHNESS (FRESH_FLUSH) CHECK CLI_WRITE");

    OpenBook * ob = static_cast<OpenBook*>(userdata);
    std::string full_path = absolut_path(path);
    int fn_ret, fd;
    fd = ob->get_local_fd(std::string(path));
    DLOG("FRESH_FLUSH GRABBING FD: %d", fd);

    // transfer file
    // bool reopen = (fi->flags & (O_WRONLY)) != 0; // need to make it RDWR
    if (true) { // i dont want to worry about it...
        fn_ret = close(fd);
        HANDLE_SYS("close failed in fresh_fetch", fn_ret)
        fd = open(full_path.c_str(), O_RDWR);
        HANDLE_SYS("reopen failed in fresh_fetch", fn_ret)
        // fi->fh = fd;
    }
    
    struct stat statbuf{};
    fn_ret = stat(full_path.c_str(), &statbuf);
    HANDLE_RET("fresh_flush stat failed in cli_write", fn_ret)
    char buf[statbuf.st_size];
    
    fn_ret = pread(fd, buf, statbuf.st_size, 0);
    HANDLE_SYS("couldnt read fresh file fresh_flush", fn_ret)

    fd_pair fdp = ob->get_fd_pair(std::string(path));
    struct fuse_file_info ser_fi{};
    ser_fi.flags = fi->flags;
    ser_fi.fh    = fdp.ser_fd;
    fn_ret = a2::watdfs_cli_write(userdata, path, buf, statbuf.st_size, 0, &ser_fi);
    HANDLE_RET("fresh_flush couldnt write", fn_ret)

    struct timespec times[2] = { statbuf.st_atim, statbuf.st_mtim };
    fn_ret = a2::watdfs_cli_utimensat(userdata, path, times);
    HANDLE_RET("utimensat rpc failed in fresh_flush", fn_ret)
    
    if (true) {
        fn_ret = close(fi->fh);
        HANDLE_SYS("close failed in fresh_fetch", fn_ret)
        fd = open(full_path.c_str(), fi->flags);
        HANDLE_SYS("reopen failed in fresh_fetch", fn_ret)
        // fi->fh = fd;
        ob->set_cli_fd(std::string(path), fd);
    }

    ob->set_validate(std::string(path), time(NULL));
    return 0;
}
