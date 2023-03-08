//
// Starter code for CS 454/654
// You SHOULD change this file
//

// C LIKE HEADERS
#include "watdfs_client.h"
#include "watdfs_make_args.h"
#include "debug.h"
#include "rpc.h"
#include "a2_client.h"
#include <time.h>
#include "upload.h"
#include "rpc_lock.h"

// SYSCALLS
#include <fcntl.h>           /* Definition of AT_* constants */
#include <sys/stat.h>

// C++ HEADERS
#include "openbook.h"
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

INIT_LOG

#define MAKE_OB OpenBook *ob = static_cast<OpenBook*>(userdata)

std::string CACHE_PATH{};
std::mutex CACHE_MUT{};

std::string absolut_path(const char *path) {
    return CACHE_PATH + std::string(path);
}

// freshness requires that both files exist
// check other conditions for short circuiting 
bool freshness_check(void *userdata, const char *path) {
    std::string full_path = absolut_path(path);
    OpenBook * ob = static_cast<OpenBook *>(userdata);
    // int T, tc, T_server, T_client;
    // current time T
    time_t T  = time(NULL);
    time_t tc = ob->get_last_validated(std::string(path));
    time_t t  = ob->get_interval();
    if (T - tc < t) return true;
    
    // else, need to check T_client and T_server
    struct stat cli_statbuf, ser_statbuf;
    stat(full_path.c_str(), &cli_statbuf);
    // assume file works.
    a2::watdfs_cli_getattr(userdata, path, &ser_statbuf);
    auto T_client = cli_statbuf.st_mtim; 
    auto T_server = ser_statbuf.st_mtim;

    // set Tc to current time since its validated now
    // TODO, remove this.
    if (T_client == T_server) ob->set_validate(std::string(path), time(NULL));
    return (T_client == T_server);
}

void update_freshness(void *userdata, const char *path) {
    // when writing / transferring
    // take for granted that T_client and T_server are updatd,
    // so just update
    OpenBook * ob = static_cast<OpenBook *>(userdata);
    ob->set_validate(std::string(path), time(NULL)); // set to Now
}

bool watdfs_cli_fresh_file(void *userdata, const char *path) {
    if (!watdfs_cli_file_exists(path)) return false;
    // else 
    return freshness_check(userdata, path); // for now
}

bool is_file_open(void *userdata, const char* path) {
    OpenBook *openbook = static_cast<OpenBook *>(userdata);
    return openbook->is_open(path);
}


// SETUP AND TEARDOWN
void *watdfs_cli_init(struct fuse_conn_info *conn, const char *path_to_cache,
                      time_t cache_interval, int *ret_code) {
    // TODO: set up the RPC library by calling `rpcClientInit`.
    int rpc_init_ret = rpcClientInit();

    // TODO: check the return code of the `rpcClientInit` call
    // `rpcClientInit` may fail, for example, if an incorrect port was exported.
    if (rpc_init_ret < 0) {
        // rpcClientInit failed, so set return code to error
        *ret_code = -EINVAL;
    }

    // It may be useful to print to stderr or stdout during debugging.
    // Important: Make sure you turn off logging prior to submission!
    // One useful technique is to use pre-processor flags like:
    // # ifdef PRINT_ERR
    // std::cerr << "Failed to initialize RPC Client" << std::endl;
    // #endif
    // Tip: Try using a macro for the above to minimize the debugging code.

    // TODO Initialize any global state that you require for the assignment and return it.
    // The value that you return here will be passed as userdata in other functions.
    // In A1, you might not need it, so you can return `nullptr`.
    OpenBook *openbook = new OpenBook(path_to_cache, cache_interval);
    void *userdata = static_cast<void*>(openbook);

    // TODO: save `path_to_cache` and `cache_interval` (for A3).
    CACHE_PATH = std::string(path_to_cache);

    DLOG("CACHE_PATH INIT TO: %s", CACHE_PATH.c_str());
    DLOG("function full path makes: %s", absolut_path("/fries.txt").c_str());

    // TODO: set `ret_code` to 0 if everything above succeeded else some appropriate
    // non-zero value.
    if (rpc_init_ret == 0)
        *ret_code = 0;

    // Return pointer to global state data.
    return userdata;
}

void watdfs_cli_destroy(void *userdata) {
    // TODO: clean up your userdata state.
    // TODO: tear down the RPC library by calling `rpcClientDestroy`.
    int rpc_destroy_ret = rpcClientDestroy();
    (void)rpc_destroy_ret; // TODO... what to do with rpc_destroy_ret
    
    OpenBook * openbook = static_cast<OpenBook*>(userdata);
    delete openbook;
}

bool is_read_only(int flags) {
    return ((flags & O_RDWR) == 0) && ((flags & O_WRONLY) == 0);
}

// GET FILE ATTRIBUTES
int watdfs_cli_getattr(void *userdata, const char *path, struct stat *statbuf) {

    // return a2::watdfs_cli_getattr(userdata, path, statbuf);
    // file can:
    // - not exist on server, propagate error
    // - exist on server, not "fresh" on client,
    //   - so bring it over, and then return attr
    // DLOG("cache path string is: %s", CACHE_PATH.c_str());
    // if this doesnt work try userdata 
    std::string full_path = absolut_path(path);
    int fn_ret;

    // should work whether open or not
    // if files not open,
    // if file doesnt exist
    // or file not fresh, then transfer file
    bool check = is_file_open(userdata, path);

    OpenBook * ob = static_cast<OpenBook *>(userdata);

    // always bring file over... THEN cache
    if (!check) {
        // get file from server
        struct fuse_file_info fi{};
        fi.flags = O_RDONLY;
        fn_ret = transfer_file(userdata, path, false, &fi);
        HANDLE_RET("cli_getattr failed getting file from server", fn_ret)
        // by now, it means the file is on client or we've returned with an error,
    } else if (is_read_only(ob->get_fd_pair(std::string(path)).cli_flags) 
               && !freshness_check(userdata, path)) {
        // else file is open in RDONLY and not fresh.
        // bring it over
        int fd = ob->get_local_fd(std::string(path));
        uint64_t local_flags = ob->get_fd_pair(std::string(path)).cli_flags;
        close(fd); // close file

        // retransfer it over (will open and write and close it)
        struct fuse_file_info fi{};
        fi.flags = O_RDONLY;
        fn_ret = transfer_file(userdata, path, false, &fi);
        HANDLE_RET("cli_getattr failed getting file from server", fn_ret)

        // reopen it and set cli_fd for transparency 
        fd = open(full_path.c_str(), (int)local_flags);
        DLOG("open in getattr, fd becomes: %d", fd);
        ob->set_cli_fd(std::string(path), fd);
    }

    // else, file is fresh and on client. get attr and return it
    fn_ret = stat(full_path.c_str(), statbuf);
    return fn_ret;
}

// CREATE, OPEN AND CLOSE
int watdfs_cli_mknod(void *userdata, const char *path, mode_t mode, dev_t dev) {
    struct stat statbuf{};
    // file exists already, locally
    std::string full_path = absolut_path(path);
    // stat should return error
    if (watdfs_cli_fresh_file(userdata, path) || 
        stat(full_path.c_str(), &statbuf) == 0) return -EEXIST;

    int fn_ret = a2::watdfs_cli_mknod(userdata, path, mode, dev);
    HANDLE_RET("error returned from mknod rpc, maybe file exists", fn_ret)

    // now make it locally
    // otherwise assume successfully made file, so now, upload file locally
    // updates metadata etc
    struct fuse_file_info fi{};
    fi.flags = O_RDONLY;
    fn_ret = transfer_file(userdata, path, false, &fi);
    HANDLE_RET("error making file locally cli_mknod", fn_ret)
    return fn_ret;
}

int watdfs_cli_open(void *userdata, const char *path,
                    struct fuse_file_info *fi) {
    // bring the file over to client if needed
    if (is_file_open(userdata, path)) return -EMFILE;

    // we need to make a struct fuse_file_info 
    // to send to server and keep track of cli_fi and ser_fi
    struct fuse_file_info ser_fi{};
    ser_fi.flags = fi->flags;

    DLOG("in watdfs_cli_open, fi->flags & O_CREAT: %d", fi->flags & O_CREAT);

    // open file locally and return file descriptor
    std::string full_path = absolut_path(path);

    // add cache later
    int fn_ret = transfer_file(userdata, path, true, &ser_fi);
    HANDLE_RET("watdfs_cli_open transfer_file failed", fn_ret)

    DLOG("transfer was successful, what's the file descriptor for ser_fi? %ld", ser_fi.fh);

    int local_fd = open(full_path.c_str(), fi->flags);
    DLOG("original local open has fd: %d", local_fd);
    HANDLE_SYS("client side open failed", local_fd)
    fi->fh = local_fd;

    // TODO, keep track of ser_fi
    OpenBook *ob = static_cast<OpenBook *>(userdata);
    ob->OB_open(std::string(path), local_fd, fi->flags, ser_fi.fh, ser_fi.flags);
    // return 0 yay
    return 0;
}

int watdfs_cli_release(void *userdata, const char *path,
                       struct fuse_file_info *fi) {
    // TODO

    // get metadata
    OpenBook *ob = static_cast<OpenBook *>(userdata);
    fd_pair fdp = ob->get_fd_pair(std::string(path));

    // make server file info
    struct fuse_file_info ser_fi{};
    ser_fi.fh    = fdp.ser_fd;
    ser_fi.flags = fdp.ser_flags;
    int fn_ret;

    // flush to server if the file had any write operations
    if (fi->flags & (O_RDWR | O_WRONLY)) {
        fn_ret = watdfs_server_flush_file(userdata, path, &ser_fi);
        HANDLE_RET("flush_file failed in cli_release", fn_ret)
    }

    int fd = ob->get_local_fd((int)fi->fh);
    DLOG("FI->FH IN RELEASE: %ld", fi->fh);
    DLOG("RELEASING WITH NEW FD: %d", fd);

    // close client  
    fn_ret = close(fd); // fi->fh may be wrong
    HANDLE_SYS("close client failed in cli_release", fn_ret)
    // close server
    fn_ret = a2::watdfs_cli_release(userdata, path, &ser_fi);
    HANDLE_RET("server client release failed in cli_release", fn_ret)

    // now close
    // this should make sure -EMFILE keeps getting returned until release returns
    CACHE_MUT.lock();
    ob->OB_close(std::string(path));
    CACHE_MUT.unlock();
    return 0;
}

// READ AND WRITE DATA
int watdfs_cli_read(void *userdata, const char *path, char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    // Read size amount of data at offset of file into buf.
    // Remember that size may be greater then the maximum array size of the RPC
    // library.
    MAKE_OB;

    // ASSUMES FILE IS CORRECTLY OPENED ETC AND
    // fd is in fi->fh
    // if not fresh, bring over file
    if (is_read_only(fi->flags)
        && !freshness_check(userdata, path)) {
        DLOG("FETCHING FILE FROM SERVER IN CLI_READ");
        int fn_ret = watdfs_get_rw_lock(path, false); // read transfer
        fn_ret = fresh_fetch(userdata, path, fi);
        RLS_IF_ERR(fn_ret, false);
        HANDLE_RET("fresh_ fetch in cli_read FAILED", fn_ret)
        fn_ret = watdfs_release_rw_lock(path, false);
    }

    // because it could have changed in fressh_fetch
    int fd = ob->get_local_fd((int)fi->fh);

    // TODO 
    int bytes_read = pread(fd, (void*)buf, size, offset);
    HANDLE_RET("bytes couldnt properly be read in cli_read", bytes_read);

    // else
    return bytes_read;
}

int watdfs_cli_write(void *userdata, const char *path, const char *buf,
                     size_t size, off_t offset, struct fuse_file_info *fi) {
    // Write size amount of data at offset of file from buf.
    // Remember that size may be greater then the maximum array size of the RPC
    // library.
    OpenBook * ob = static_cast<OpenBook*>(userdata);
    int fd = ob->get_local_fd((int)fi->fh); // need it to raise error if not kept track properly

    // TODO
    int bytes_written = pwrite(fd, (void *)buf, size, offset);
    HANDLE_RET("bytes couldnt write properly in cli_write", bytes_written)

    if (!freshness_check(userdata, path)) {
        int fn_ret = watdfs_get_rw_lock(path, true);
        fn_ret = fresh_flush(userdata, path, fi);
        RLS_IF_ERR(fn_ret, true);
        HANDLE_RET("fresh_flussh FAILED in cli_write", fn_ret)
        fn_ret = watdfs_release_rw_lock(path, true);
    }

    DLOG("WRITE SUCCEEDED WITH FD: %d", fd);

    // TODO fsync to server if does not pass freshness check
    return bytes_written;
}

// this is a write call
int watdfs_cli_truncate(void *userdata, const char *path, off_t newsize) {
    // Change the file size to newsize.
    // if (!watdfs_cli_file_exists(path)) return -ENOENT; // file DNE

    // grab file from server 
    // idk what to do with fi yet
    OpenBook *ob = static_cast<OpenBook *>(userdata);
    std::string full_path = absolut_path(path);
    int fn_ret;

    if (!watdfs_cli_file_exists(path) || !ob->is_open(path)) {
        // bring it over... will return error if dne on server 
        struct fuse_file_info fi{};
        fi.flags = O_RDWR;
        fn_ret = transfer_file(userdata, path, false, &fi);
        HANDLE_RET("couldn't fetch file for truncate", fn_ret)
    } else if (!freshness_check(userdata, path)
               && is_read_only(ob->get_fd_pair(std::string(path)).cli_flags))
    {
        struct fuse_file_info fi{};
        fi.flags = O_RDWR;
        fn_ret = transfer_file(userdata, path, false, &fi);
        HANDLE_RET("couldn't fetch file for truncate", fn_ret)
    } else {
        DLOG("DIDNT TRANSFER FILE IN TRUNCATE");
    }

    // else file is here and is "fresh" (or open for write)
    fn_ret = truncate(full_path.c_str(), newsize);
    HANDLE_SYS("couldn't truncate local file in cli_truncate", fn_ret)

    // TODO add freshness check push to server if timed out...

    if (!freshness_check(userdata, path)) {
        fn_ret = watdfs_get_rw_lock(path, true); 
        RLS_IF_ERR(fn_ret, true);
        HANDLE_RET("couldn't get rw_lock for cli_truncate", fn_ret)

        // fn_ret = fresh_flush(userdata, path, &fi);
        // all you did was change 
        fn_ret = a2::watdfs_cli_truncate(userdata, path, newsize);
        RLS_IF_ERR(fn_ret, true);
        HANDLE_RET("error updating metadata on server file", fn_ret)

        fn_ret = watdfs_release_rw_lock(path, true);
        RLS_IF_ERR(fn_ret, true);
        HANDLE_RET("coulnd't release rw_lock for cli_truncate", fn_ret)
    }

    return 0;
}

// this is a write call, need to make sure file is open for write
int watdfs_cli_fsync(void *userdata, const char *path,
                     struct fuse_file_info *fi) {
    // Get server fd
    OpenBook *ob = static_cast<OpenBook *>(userdata);
    // fd_pair fdp = ob->get_fd_pair(std::string(path));

    DLOG("in fsync, fi->fh is: %ld, and saved ser_fd is: %d", 
         fi->fh, 
         ob->get_server_fd(std::string(path)));

    // make server file info
    // struct fuse_file_info ser_fi{};
    // ser_fi.fh    = fdp.ser_fd;
    // ser_fi.flags = fdp.ser_flags;

    // DLOG("ser_fi.fh is: %ld", ser_fi.fh);
    int fn_ret = watdfs_get_rw_lock(path, true); // for write
    fn_ret = fresh_flush(userdata, path, fi);
    RLS_IF_ERR(fn_ret, true);
    fn_ret = watdfs_release_rw_lock(path, true);
    HANDLE_RET("couldn't flush file to server in cli_fsync", fn_ret)
    return 0;
}

// CHANGE METADATA
// this is a write call, need to open
int watdfs_cli_utimensat(void *userdata, const char *path,
                         const struct timespec ts[2]) {
    // 
    MAKE_OB;

    // get file if not on client
    // will get changed to O_RDWR in transfer_file
    int fn_ret;

    // set it locally
    std::string full_path = absolut_path(path);

    if (!watdfs_cli_file_exists(path) || !ob->is_open(path)) {
        // bring it over... will return error if dne on server 
        struct fuse_file_info fi{};
        fi.flags = O_RDWR;
        fn_ret = transfer_file(userdata, path, false, &fi);
        HANDLE_RET("couldn't fetch file for truncate", fn_ret)
    } else if (!freshness_check(userdata, path)
               && is_read_only(ob->get_fd_pair(std::string(path)).cli_flags)) 
    {
        struct fuse_file_info fi{};
        fi.flags = O_RDWR;
        fn_ret = transfer_file(userdata, path, false, &fi);
        HANDLE_RET("couldn't fetch file for utimensat", fn_ret)
    } else {
        DLOG("DIDNT TRANSFER FILE IN UTIMENSAT");
        // fresh_check = is_read_only(ob->get_fd_pair(std::string(path)).cli_flags);
    }

    fn_ret = utimensat(0, full_path.c_str(), ts, 0);
    HANDLE_SYS("utimensat client failed with errno", fn_ret)

    // TODO freshness check
    if (!freshness_check(userdata, path)) {
        // TODO flush to server
        fn_ret = watdfs_get_rw_lock(path, true); 
        RLS_IF_ERR("error getting rw lock for utimensat", true);
        HANDLE_RET("coulnd't get rw_lock for cli_utimensat", fn_ret)

        fn_ret = a2::watdfs_cli_utimensat(userdata, path, ts);
        HANDLE_RET("error updating metadata on server file", fn_ret)

        fn_ret = watdfs_release_rw_lock(path, true);
        RLS_IF_ERR("error releasing rw lock for utimensat", true);
        HANDLE_RET("coulnd't release rw_lock for cli_utimensat", fn_ret)
    }

    return 0;
}
