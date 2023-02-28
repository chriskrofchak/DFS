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

std::string full_cache_path(const char* path) {
    return CACHE_PATH + std::string(path);
} 

#define HANDLE_RET(msg, ret)                     \
  if (ret < 0) {                            \
      DLOG("failed with message %s\n and retcode %d", msg, ret);  \
      return ret;                           \
  }

class OpenBook {
    // filename, bool open for Write.
    std::unordered_map<std::string, bool> open_files;
    OpenBook() : open_files() {}
    ~OpenBook() {}

    // open and close adds and removes from OpenBook
    int OB_open(std::string filename, bool forWrite);
    int OB_close(std::string filename);
};

bool watdfs_cli_fresh_file(const char *path) {
    struct stat statbuf{};
    int fn_ret = a2::watdfs_cli_getattr(nullptr, path, &statbuf);
    if (fn_ret < 0) {
        if (fn_ret == -ENOENT) return 0;
        // else, it was another error...
        HANDLE_RET("failed to properly check if file exists on server (cli_file_exists)", fn_ret)
        return fn_ret;
    }
    return 1;
}

// NOW FOR A3.... HAVE THESE CHANGES
// ASSUME FILE EXISTS
int watdfs_cli_transfer_file(void *userdata, const char *path, struct fuse_file_info *fi) {
    // GET ATTR
    struct stat statbuf{};

    int fn_ret = a2::watdfs_cli_getattr(userdata, path, &statbuf);
    HANDLE_RET("getattr rpc failed in transfer", fn_ret)

    // TRUNCATE FILE TO DESIRED SIZE
    std::string full_path = full_cache_path(path);
    fn_ret = truncate(full_path.c_str(), statbuf.st_size);
    HANDLE_RET("client side truncate failed in cli_transfer", fn_ret)

    // READ FILE INTO CACHE 
    char buf[statbuf.st_size];
    // struct fuse_file_info fi{};
    // fi.flags = O_CREAT | O_RDONLY;
    fn_ret = a2::watdfs_cli_open(userdata, path, fi);
    HANDLE_RET("open failed in cli_transfer", fn_ret)

    fn_ret = a2::watdfs_cli_read(userdata, path, buf, statbuf.st_size, 0, fi);
    HANDLE_RET("cli_read rpc failed in cli_transfer", fn_ret)

    // write to client
    int fd = open(full_path.c_str(), O_RDWR | O_CREAT);
    fn_ret = write(fd, buf, statbuf.st_size);
    HANDLE_RET("client open and write failed in cli_transfer", fn_ret)

    // update file metadata(?) TODO
    // update times? 
    struct timespec times[2] = { statbuf.st_atime, statbuf.st_mtime };
    fn_ret = utimensat(fd, full_path.c_str(), times, 0);
    HANDLE_RET("updating metadata on client file failed in cli_transfer", fn_ret)

    fn_ret = close(fd);
    HANDLE_RET("client close failed in cli_transfer", fn_ret)
    return 0;
}

// ASSUMES THAT THE FILE IS IN THE CORRECT PATH
int watdfs_server_flush_file(void *userdata, const char *path, struct fuse_file_info *fi) {
    // get file locally
    std::string full_path = full_cache_path(path);

    // read into buf
    struct stat statbuf{};
    int fn_ret = stat(full_path.c_str(), &statbuf);
    HANDLE_RET("client stat failed in flush_file", fn_ret)
    int fd = open(full_path.c_str(), O_RDONLY);
    char buf[statbuf.st_size];

    fn_ret = read(fd, (void *)buf, statbuf.st_size);
    HANDLE_RET("client local read failed in flush_file", fn_ret)

    // need to open it on the server
    // struct fuse_file_info fi{};
    // fi.flags = O_CREAT | O_WRONLY;
    // fn_ret = a2::watdfs_cli_open(userdata, path, &fi);
    // HANDLE_RET("getting file failed in flush_file", fn_ret)

    // write buf on server
    // todo fix fuse_file_info
    fn_ret = a2::watdfs_cli_write(userdata, path, buf, statbuf.st_size, 0, fi);
    HANDLE_RET("write rpc failed in flush_file", fn_ret)

    // update times on server
    struct timespec times[2] = { statbuf.st_atime, statbuf.st_mtime };
    fn_ret = a2::watdfs_cli_utimensat(userdata, path, times);
    HANDLE_RET("utimensat rpc failed in flush_file", fn_ret)
    // close on the server
    fn_ret = a2::watdfs_cli_release(userdata, path, fi);
    HANDLE_RET("close rpc failed in flush_file", fn_ret)
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
    void *userdata = nullptr;

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
}

// GET FILE ATTRIBUTES
int watdfs_cli_getattr(void *userdata, const char *path, struct stat *statbuf) {
    // file can:
    // - not exist on server, propagate error
    // - exist on server, not "fresh" on client,
    //   - so bring it over, and then return attr

    if (!watdfs_cli_fresh_file(path)) {
        // get file from server
    }

    // else, file is fresh and on client. get attr and return it

    return a2::watdfs_cli_getattr(userdata, path, statbuf);
}

// CREATE, OPEN AND CLOSE
int watdfs_cli_mknod(void *userdata, const char *path, mode_t mode, dev_t dev) {
    return a2::watdfs_cli_mknod(userdata, path, mode, dev);
}

int watdfs_cli_open(void *userdata, const char *path,
                    struct fuse_file_info *fi) {
    // bring the file over to client if needed
    int fn_ret = watdfs_cli_transfer_file(userdata, path, fi);
    HANDLE_RET("watdfs_cli_open failed", fn_ret)

    // open file locally and return file descriptor
    std::string full_path = full_cache_path(path);
    int local_fd = open(full_path.c_str(), fi->flags);
    fi->fh = local_fd;

    return 0;
}

int watdfs_cli_release(void *userdata, const char *path,
                       struct fuse_file_info *fi) {
    // TODO
    int fn_ret = close(fi->fh);

    // flush to server if the file had any write operations
    if (fi->flags & (O_RDWR | O_WRONLY)) {
        fn_ret = watdfs_server_flush_file(userdata, path, fi);
        HANDLE_RET("flush_file failed in cli_release", fn_ret)
    }

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

    int fn_ret = watdfs_cli_transfer_file(userdata, path, &fi);
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
    int fn_ret = watdfs_cli_transfer_file(userdata, path, &fi);
    (void)fn_ret; // TODO use later

    // get attr ?? using fn call or from scratch, now that file is here
    // then 
    
    // set it locally
    std::string full_path = full_cache_path(path);

    // flush it to server
    return -1; // TODO
}
