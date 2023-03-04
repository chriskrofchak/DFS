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

// SYSCALLS
#include <fcntl.h>           /* Definition of AT_* constants */
#include <sys/stat.h>

// C++ HEADERS
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

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

#define RLS_IF_ERR(fn_ret, is_write) if (fn_ret < 0) watdfs_release_rw_lock(path, is_write)

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
    std::string cache_path_;
    time_t t; // this is little t
    time_t Tc;
    std::unordered_map<std::string, fd_pair> open_files;
public:
    OpenBook(const char* path, time_t cache_int) : 
        cache_path_(std::string(path)),
        t(cache_int),
        Tc(0),
        open_files() {}
    ~OpenBook() {}

    // open and close adds and removes from OpenBook
    int OB_open(std::string filename, int cli_fd, uint64_t cli_flags, int ser_fd, uint64_t ser_flags);
    int OB_close(std::string filename);
    fd_pair get_fd_pair(std::string filename) { return open_files.at(filename); }
    std::string get_cache() { return cache_path_; }
    time_t get_interval() { return t; }
    time_t get_last_validated() { return Tc; }
    void set_validate(time_t new_Tc) { Tc = new_Tc; };
    bool is_open(const char *path) { return open_files.count(std::string(path)); }
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
    open_files.insert(std::pair<std::string, fd_pair>(filename, fdp));
    return 0;
}

int OpenBook::OB_close(std::string filename) {
    open_files.erase(filename);
    return 0;
}

bool watdfs_cli_file_exists(const char *path) {
    struct stat statbuf{};
    int fn_ret = stat(path, &statbuf);
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

// freshness requires that both files exist
// check other conditions for short circuiting 
bool freshness_check(void *userdata, const char *path) {
    OpenBook * ob = static_cast<OpenBook *>(userdata);
    // int T, tc, T_server, T_client;
    // current time T
    time_t T  = time(NULL);
    time_t tc = ob->get_last_validated();
    time_t t  = ob->get_interval();
    if (T - tc < t) return true;
    
    // else, need to check T_client and T_server
    struct stat cli_statbuf, ser_statbuf;
    stat(path, &cli_statbuf);
    a2::watdfs_cli_getattr(userdata, path, &ser_statbuf);
    auto T_client = cli_statbuf.st_mtim; 
    auto T_server = ser_statbuf.st_mtim;
    return (T_client == T_server);
}

bool watdfs_cli_fresh_file(void *userdata, const char *path) {
    // (for testing before freshness checks)
    // TODO CHANGE BACK
    /*
    logic: 
    if (! file exists ) return false; // not fresh if DNE
    // else, file exists, check if fresh
    return freshness_check(userdata, path); 
    */
    return watdfs_cli_file_exists(path); // for now
}

bool is_file_open(void *userdata, const char* path) {
    OpenBook *openbook = static_cast<OpenBook *>(userdata);
    return openbook->is_open(path);
}

// FOR ATOMIC FILE TRANSFERS
int watdfs_get_rw_lock(const char *path, bool is_write) {
    // will return 0 if you get the lock
    void **args = new void*[3];
    int arg_types[4];

    // need path because we'll do it by file
    int pathlen = strlen(path) + 1;
    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;

    int lock_mode = is_write ? 1 : 0; // 1 for write, 0 for read

    // lock mode
    arg_types[1] = encode_arg_type(true, false, false, ARG_INT, 0);
    args[1] = (void*)(&lock_mode);

    int retcode = 0;
    // return value
    arg_types[2] = encode_retcode();
    args[2] = (void *)&retcode; // set retcode to 0 initially...

    arg_types[3] = 0; // null terminate arg_types

    int rpc_ret = RPCIFY("get_rw_lock");
    if (rpc_ret < 0) {
        DLOG("rpc call get_rw_lock failed!");
        retcode = -EINVAL; // to know that rpc failed...
    }

    // else delete args and return retcode as normal 
    delete[] args;
    return retcode;
}

int watdfs_release_rw_lock(const char *path, bool is_write) {
    // will return 0 if successfully released the lock
    // will return 0 if you get the lock
    void **args = new void*[3];
    int arg_types[4];

    // need path
    int pathlen = strlen(path) + 1;
    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;

    int lock_mode = is_write ? 1 : 0; // 1 for write, 0 for read

    // lock mode
    arg_types[1] = encode_arg_type(true, false, false, ARG_INT, 0);
    args[1] = (void*)(&lock_mode);

    int retcode = 0;
    // return value
    arg_types[2] = encode_retcode();
    args[2] = (void *)&retcode; // set retcode to 0 initially...

    arg_types[3] = 0; // null terminate arg_types

    int rpc_ret = RPCIFY("release_rw_lock");
    if (rpc_ret < 0) {
        DLOG("rpc call release_rw_lock failed!");
        retcode = -EINVAL; // to know that rpc failed...
    }

    delete[] args;
    return retcode;
}

// NOW FOR A3.... HAVE THESE CHANGES
// not done when file is already open,
int transfer_file(void *userdata, const char *path, bool persist_fd, struct fuse_file_info *fi) {
    // if files open, stop
    if (is_file_open(userdata, path)) return -EMFILE;

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

    //////////
    // CLIENT WRITE

    // file may not exist on client side, so just "open" with O_CREAT
    // to create it if it doesnt exist, otherwise this does nothing
    std::string full_path = absolut_path(path);

    // DLOG("FULL_PATH STRING IS: %s", full_path.c_str());
    if (!watdfs_cli_file_exists(full_path.c_str())) {
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
        fd = ob->get_fd_pair(std::string(path)).cli_fd; 
    } else {
        int fd = open(full_path.c_str(), O_RDONLY);
        HANDLE_SYS("opening file in flush_file failed", fd)
    }
    char buf[statbuf.st_size];

    ssize_t b_read = read(fd, (void *)buf, statbuf.st_size);
    HANDLE_SYS("client local read failed in flush_file", b_read)

    // this should be after a freshness check (if fresh, return 0)
    // else : lock and write

    // ATOMIC WRITES
    fn_ret = watdfs_get_rw_lock(path, true); // needs to write

    // write buf on server
    // todo fix fuse_file_info
    DLOG("getting ESPIPE, whats fi->fh?: %ld", fi->fh);
    fn_ret = a2::watdfs_cli_write(userdata, path, buf, statbuf.st_size, 0, fi);

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

    if (!watdfs_cli_fresh_file(userdata, full_path.c_str())) {
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
    std::string full_path = absolut_path(path);
    // stat should return error
    if (watdfs_cli_fresh_file(userdata, full_path.c_str()) || 
        stat(full_path.c_str(), &statbuf) == 0) return -EEXIST;

    int fn_ret = a2::watdfs_cli_mknod(userdata, path, mode, dev);
    HANDLE_RET("error returned from mknod rpc, maybe file exists", fn_ret)

    // now make it locally
    // otherwise assume successfully made file, so now, upload file locally
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

    if (!watdfs_cli_fresh_file(userdata, full_path.c_str())) {
        int fn_ret = transfer_file(userdata, path, true, &ser_fi);
        HANDLE_RET("watdfs_cli_open transfer_file failed", fn_ret)
    } else {
        // didn't transfer file over, so there is no ser_fi.fh flag
        // need to open it on server and preserve fd? 
        int fn_ret = a2::watdfs_cli_open(userdata, path, &ser_fi);
        HANDLE_RET("couldn't open file (permissions from server)", fn_ret)
        // DLOG("didn't transfer file, so...");
    }

    DLOG("transfer was successful, what's the file descriptor for ser_fi? %ld", ser_fi.fh);

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

    // TODO fsync to server if does not pass freshness check
    return bytes_written;
}

// this is a write call
int watdfs_cli_truncate(void *userdata, const char *path, off_t newsize) {
    // Change the file size to newsize.

    // grab file from server 
    // idk what to do with fi yet

    int fn_ret = truncate(path, newsize);
    HANDLE_SYS("couldn't truncate local file in cli_truncate", fn_ret)

    // TODO add freshness check push to server if timed out...
    if (true) {
        fn_ret = watdfs_get_rw_lock(path, true); 
        RLS_IF_ERR("error getting rw lock for truncate", true);
        HANDLE_RET("couldn't get rw_lock for cli_truncate", fn_ret)

        fn_ret = a2::watdfs_cli_truncate(userdata, path, newsize);
        HANDLE_RET("error updating metadata on server file", fn_ret)

        fn_ret = watdfs_release_rw_lock(path, true);
        RLS_IF_ERR("error releasing rw lock for truncate", true);
        HANDLE_RET("coulnd't release rw_lock for cli_truncate", fn_ret)
    }

    return 0;
}

// this is a write call, need to make sure file is open for write
int watdfs_cli_fsync(void *userdata, const char *path,
                     struct fuse_file_info *fi) {
    // TODO
    int fn_ret = watdfs_server_flush_file(userdata, path, fi);
    HANDLE_RET("couldn't flush file to server in cli_fsync", fn_ret)

    return 0;
}

// CHANGE METADATA
// this is a write call, need to open
int watdfs_cli_utimensat(void *userdata, const char *path,
                         const struct timespec ts[2]) {
    // get file if not on client
    struct fuse_file_info fi{};
    fi.flags = O_WRONLY; // will get changed to O_RDWR in transfer_file
    int fn_ret;

    // set it locally
    std::string full_path = absolut_path(path);

    // if file not fresh
    if (!watdfs_cli_fresh_file(userdata, full_path.c_str())) {
        fn_ret = transfer_file(userdata, path, false, &fi);
        HANDLE_RET("couldn't fetch file for utimensat", fn_ret)
    }

    fn_ret = utimensat(0, full_path.c_str(), ts, 0);
    HANDLE_SYS("utimensat client failed with errno", fn_ret)

    // TODO freshness check
    if (true) {
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
