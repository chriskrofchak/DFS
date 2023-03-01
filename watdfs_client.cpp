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

// SYSCALLS
#include <fcntl.h>           /* Definition of AT_* constants */
#include <sys/stat.h>

// C++ HEADERS
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>

INIT_LOG

std::string CACHE_PATH{};
std::mutex CACHE_MUT{};

std::string absolut_path(const char* path) {
    return CACHE_PATH + std::string(path);
} 

#define HANDLE_RET(msg, ret)                                               \
  if (ret < 0) {                                                           \
      DLOG("failed with message:\n    %s\n    and retcode %d", msg, ret);  \
      return ret;                                                          \
  }

#define HANDLE_SYS(msg, ret)                                                         \
  if (ret < 0) {                                                                     \
      DLOG("syscall failed with message:\n    %s\n    and errcode %d", msg, errno);  \
      return -errno;                                                                 \
  }

struct fd_pair {
    int cli_fd, ser_fd;
    uint64_t cli_flags, ser_flags;
    fd_pair(int cfd, int sfd, uint64_t cfl, uint64_t sfl) : 
        cli_fd(cfd),
        ser_fd(sfd),
        cli_flags(cfl),
        ser_flags(sfl)
    {}
};

class OpenBook {
    // map for filename, client file descriptor fd, and client flags.
    std::unordered_map<std::string, fd_pair> open_files;
public:
    OpenBook() : open_files() {}
    ~OpenBook() {}

    // open and close adds and removes from OpenBook
    int OB_open(std::string filename, int cli_fd, uint64_t cli_flags, int ser_fd, uint64_t ser_flags);
    int OB_close(std::string filename);
    fd_pair get_fd_pair(std::string filename) { return open_files.at(filename); }
    bool is_open(const char *path); 
};

int OpenBook::OB_open(
    std::string filename, 
    int cli_fd, 
    uint64_t cli_flags, 
    int ser_fd, 
    uint64_t ser_flags) 
{
    if (open_files.count(filename)) return -EMFILE; // BAD
    // else 
    fd_pair fdp(cli_fd, ser_fd, cli_flags, ser_flags);
    // open_files.insert({filename, fdp});
    return 0;
}

bool watdfs_cli_fresh_file(const char *path) {
    return false; // we always want to bring it over
    // (for testing before freshness checks)
    // TODO CHANGE BACK
    // struct stat statbuf{};
    // int fn_ret = stat(path, &statbuf);
    // if (fn_ret < 0) {
    //     if (fn_ret == -ENOENT) return false;
    // }
    // // to do, freshness checks
    // return true;
}

bool is_file_open(void *userdata, const char* path) {
    OpenBook *openbook = static_cast<OpenBook *>(userdata);
    return openbook->is_open(path);
}

// NOW FOR A3.... HAVE THESE CHANGES
// not done when file is already open,
int transfer_file(void *userdata, const char *path, bool persist_fd, struct fuse_file_info *fi) {
    // if files open, stop
    if (is_file_open(userdata, path)) return -EMFILE;

    // getattr
    struct stat statbuf{};
    int fn_ret = a2::watdfs_cli_getattr(userdata, path, &statbuf);
    HANDLE_RET("client stat returned error in transfer_file", fn_ret)

    // SO, file exists on server, read from it and then 
    // update client file

    ////////
    // SERVER FETCH

    //  read 
    // you can concurrently open in read so this should work even for getattr
    
    char buf[statbuf.st_size];
    if (fi->flags & O_WRONLY) fi->flags = (fi->flags & ~O_WRONLY) | O_RDWR; // since we need to read too
    fn_ret = a2::watdfs_cli_open(userdata, path, fi);
    HANDLE_RET("open failed in cli_transfer", fn_ret) // this errror is normal if ENOFILE
    fn_ret = a2::watdfs_cli_read(userdata, path, buf, statbuf.st_size, 0, fi);
    HANDLE_RET("cli_read rpc failed in cli_transfer", fn_ret)
    
    // if we are just bringing it over to read/ not an open call,
    // we don't need to keep the fd.
    if (!persist_fd) {
        fn_ret = a2::watdfs_cli_release(userdata, path, fi); // close our FD / release
        HANDLE_RET("cli_release in transfer failed", fn_ret)
    }

    //////////
    // CLIENT WRITE

    // file may not exist on client side, so just "open" with O_CREAT
    // to create it if it doesnt exist, otherwise this does nothing
    std::string full_path = absolut_path(path);
    int fd = open(full_path.c_str(), O_RDWR | O_CREAT);
    HANDLE_SYS("client open failed in cli_transfer", fd)
    fn_ret = close(fd);
    HANDLE_SYS("client first close failed in transfer", fn_ret)

    // truncate file client side
    fn_ret = truncate(full_path.c_str(), statbuf.st_size);
    HANDLE_SYS("client side truncate failed in cli_transfer", fn_ret)
    

    // write client side
    fd = open(full_path.c_str(), O_WRONLY);
    HANDLE_SYS("client open failed in cli_transfer", fn_ret)
    ssize_t b_wrote = write(fd, buf, statbuf.st_size);
    HANDLE_SYS("client write failed in cli_transfer", b_wrote)

    // update file metadata(?) TODO
    // update times? 
    struct timespec times[2] = { statbuf.st_atime, statbuf.st_mtime };
    fn_ret = utimensat(fd, full_path.c_str(), times, 0);
    HANDLE_SYS("updating metadata on client file failed in cli_transfer", fn_ret)

    fn_ret = close(fd);
    HANDLE_SYS("client close failed in cli_transfer", fn_ret)
    return 0;
}

// ASSUMES THAT THE FILE IS IN THE CORRECT PATH
int watdfs_server_flush_file(void *userdata, const char *path, struct fuse_file_info *fi) {
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
    int fd = open(full_path.c_str(), O_RDONLY);
    char buf[statbuf.st_size];

    ssize_t b_read = read(fd, (void *)buf, statbuf.st_size);
    HANDLE_RET("client local read failed in flush_file", b_read)

    // NO OPEN because file should ALREADY BE OPEN if you're WRITING TO IT
    // subject to change...

    // write buf on server
    // todo fix fuse_file_info
    fn_ret = a2::watdfs_cli_write(userdata, path, buf, statbuf.st_size, 0, fi);
    HANDLE_RET("write rpc failed in flush_file", fn_ret)

    // update times on server
    struct timespec times[2] = { statbuf.st_atime, statbuf.st_mtime };
    fn_ret = a2::watdfs_cli_utimensat(userdata, path, times);
    HANDLE_RET("utimensat rpc failed in flush_file", fn_ret)

    // close on the server, not the job of flush_file
    // fn_ret = a2::watdfs_cli_release(userdata, path, fi);
    // HANDLE_RET("close rpc failed in flush_file", fn_ret)
    return 0;
}


// SETUP AND TEARDOWN
void *watdfs_cli_init(struct fuse_conn_info *conn, const char *path_to_cache,
                      time_t cache_interval, int *ret_code) {
    // TODO: set up the RPC library by calling `rpcClientInit`.
    int rpc_init_ret = rpcClientInit();

    CACHE_PATH = std::string(path_to_cache);

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
    OpenBook *openbook = new OpenBook();
    void *userdata = static_cast<void*>(openbook);

    // TODO: save `path_to_cache` and `cache_interval` (for A3).

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

// GET FILE ATTRIBUTES
int watdfs_cli_getattr(void *userdata, const char *path, struct stat *statbuf) {
    // file can:
    // - not exist on server, propagate error
    // - exist on server, not "fresh" on client,
    //   - so bring it over, and then return attr
    std::string full_path = absolut_path(path);
    int fn_ret;

    if (!watdfs_cli_fresh_file(path)) {
        // get file from server
        struct fuse_file_info fi{};
        fi.flags = O_RDONLY;
        fn_ret = transfer_file(userdata, path, false, &fi);
        HANDLE_RET("cli_getattr failed getting file from server", fn_ret)
        // by now, it means the file is on client or we've returned with an error,
    }

    // else, file is fresh and on client. get attr and return it
    fn_ret = stat(full_path.c_str(), statbuf);

    return fn_ret;
}

// CREATE, OPEN AND CLOSE
int watdfs_cli_mknod(void *userdata, const char *path, mode_t mode, dev_t dev) {
    struct stat statbuf{};
    // file exists already, locally
    if (watdfs_cli_fresh_file(path) || !stat(path, &statbuf)) return -EEXIST;

    int fn_ret = a2::watdfs_cli_mknod(userdata, path, mode, dev);
    HANDLE_RET("error returned from mknod rpc, maybe file exists", fn_ret)

    // now make it locally
    // otherwise assume successfully made file, so now, upload file locally
    struct fuse_file_info fi{};
    fi.flags = O_RDONLY;
    fn_ret = transfer_file(userdata, path, false, &fi);
    HANDLE_RET("error making file locally cli_mknod", fn_ret)

    // good and done, return the transfer return
    return fn_ret;
}

int watdfs_cli_open(void *userdata, const char *path,
                    struct fuse_file_info *fi) {
    // bring the file over to client if needed

    // we need to make a struct fuse_file_info 
    // to send to server and keep track of cli_fi and ser_fi
    struct fuse_file_info ser_fi{};
    ser_fi.flags = fi->flags;

    if (!watdfs_cli_fresh_file(path)) {
        int fn_ret = transfer_file(userdata, path, true, &ser_fi);
        HANDLE_RET("watdfs_cli_open transfer_file failed", fn_ret)
    }

    // open file locally and return file descriptor
    std::string full_path = absolut_path(path);
    int local_fd = open(full_path.c_str(), fi->flags);
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
    ob->OB_close(std::string(path));

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

    // close client  
    fn_ret = close(fi->fh);
    HANDLE_SYS("close client failed in cli_release", fn_ret)
    // close server
    fn_ret = a2::watdfs_cli_release(userdata, path, &ser_fi);
    HANDLE_RET("server client release failed in cli_release", fn_ret)

    return 0;
}

// READ AND WRITE DATA
int watdfs_cli_read(void *userdata, const char *path, char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    // Read size amount of data at offset of file into buf.
    // Remember that size may be greater then the maximum array size of the RPC
    // library.

    // ASSUMES FILE IS CORRECTLY OPENED ETC AND
    // fd is in fi->fh

    // TODO 
    int bytes_read = pread(fi->fh, (void*)buf, size, offset);
    HANDLE_RET("bytes couldnt properly be read in cli_read", bytes_read);

    // else
    return bytes_read;
}

int watdfs_cli_write(void *userdata, const char *path, const char *buf,
                     size_t size, off_t offset, struct fuse_file_info *fi) {
    // Write size amount of data at offset of file from buf.

    // Remember that size may be greater then the maximum array size of the RPC
    // library.

    // TODO
    int bytes_written = pwrite(fi->fh, (void *)buf, size, offset);
    HANDLE_RET("bytes couldnt write properly in cli_write", bytes_written)

    return bytes_written;
}

int watdfs_cli_truncate(void *userdata, const char *path, off_t newsize) {
    // Change the file size to newsize.

    // grab file from server 
    // idk what to do with fi yet
    struct fuse_file_info fi{};

    int fn_ret = transfer_file(userdata, path, false, &fi);
    HANDLE_RET("transferring file failed in cli_truncate", fn_ret)

    return fn_ret;
}

int watdfs_cli_fsync(void *userdata, const char *path,
                     struct fuse_file_info *fi) {
    // TODO
    int fn_ret = watdfs_server_flush_file(userdata, path, fi);
    HANDLE_RET("cli_fsync flush failed", fn_ret)

    return fn_ret;
}

// CHANGE METADATA
int watdfs_cli_utimensat(void *userdata, const char *path,
                         const struct timespec ts[2]) {
    // get file if not on client
    struct fuse_file_info fi{};
    fi.flags = O_WRONLY; // will get changed to O_RDWR in transfer_file
    int fn_ret;

    // if file not fresh
    if (!watdfs_cli_fresh_file(path)) {
        fn_ret = transfer_file(userdata, path, false, &fi);
        HANDLE_RET("couldn't fetch file for utimensat", fn_ret)
    }

    // set it locally
    std::string full_path = absolut_path(path);
    fn_ret = utimensat(0, full_path.c_str(), ts, 0);
    HANDLE_SYS("utimensat client failed with errno", fn_ret)

    // TODO flush to server
    // flush it to server
    // fn_ret = watdfs_server_flush_file(userdata, path, nullptr);
    return a2::watdfs_cli_utimensat(userdata, path, ts);
}
