//
// Starter code for CS 454/654
// You SHOULD change this file
//

// local headers 
#include "watdfs_make_args.h"
#include "rpc.h"
#include "debug.h"
#include "rw_lock.h"

INIT_LOG

// C headers
#include <fuse.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <cstdlib>

// C++ headers
#include <mutex>
#include <algorithm>
#include <string>
#include <unordered_map>
// #include <unordered_set>
#include <utility>
#include <memory>

// Global state server_persist_dir.
char *server_persist_dir = nullptr;
std::mutex persist_mut{};

// rw_lock_t rw_lock{};

#define PROLOGUE                                   \
    char *short_path = (char *)args[0];            \
    char *full_path  = get_full_path(short_path)

#define SETUP_SERVER_ARG(__ARG_COUNT) \
    MAKE_ARG_TYPES(__ARG_COUNT); SETUP_ARG_TYPES(__ARG_COUNT, "1") 

#define EPILOGUE(ret, fn) free(full_path); DLOG("Returning from %s with code: %d", fn, *ret)

#define RPC_REG(fn_string, fn_name) rpcRegister((char *)fn_string, arg_types, fn_name)

#define UPDATE_RET if (sys_ret < 0) *ret = -errno


// C++ Wrapper for rw_lock_t
// so that destructor is called in a C++ way
class RWLock {
    std::unique_ptr<rw_lock_t> rw_lock;
public:
    RWLock() : rw_lock(std::make_unique<rw_lock_t>()) {
        rw_lock_init(rw_lock.get());
    }
    ~RWLock() {
        rw_lock_destroy(rw_lock.get());
    }
    int lock(rw_lock_mode_t mode) {
        return rw_lock_lock(rw_lock.get(), mode);
    }
    int release(rw_lock_mode_t mode) {
        return rw_lock_unlock(rw_lock.get(), mode);
    }
};

// thread safe file tracker using mutex m
class ServerBook {
    std::mutex m;
    std::unordered_multimap<std::string, int> open_files;
    std::unordered_map<std::string, int>  write_fd;
    std::unordered_map<std::string, std::unique_ptr<RWLock> > file_locks;
public:
    ServerBook() : m(), open_files(), write_fd(), file_locks() {}
    ~ServerBook() {} // default. and unique ptr will destroy locks for us

    // so you can lock and unlock it
    void lock() { m.lock(); }
    void unlock() { m.unlock(); }

    // 
    bool is_file_open(std::string path) {
        return (open_files.count(path) > 0);
    }
    bool is_file_open(const char *path) {
        return is_file_open(std::string(path));
    }

    // should always be same fd
    int get_fd(std::string path) {
        return open_files.find(path)->second;
    }
    int get_fd(const char *path) {
        return get_fd(std::string(path));
    }

    // overload two
    void open_file(std::string path, int fd, int mode) {
        // m.lock();
        DLOG("filebook.open_file called on file: %s", path.c_str());
        bool is_write = (mode & (O_RDWR | O_WRONLY)) != 0;
        open_files.insert(std::pair<std::string, int>(path, fd));
        if (is_write)
            write_fd.insert(std::pair<std::string, int>(path, fd));
        // m.unlock();
    }
    void open_file(const char *path, int fd, int mode) {
        open_file(std::string(path), fd, mode);
    }

    // overload two
    void close_file(std::string path, int fd, int mode) {
        // m.lock();
        DLOG("filebook.close_file called on file: %s", path.c_str());
        if (!open_files.count(path)) return; // just so no segfault...

        std::unordered_multimap<std::string, int>::iterator it = open_files.find(path);
        open_files.erase(it); // erases one element

        if ((mode & (O_RDWR | O_WRONLY)) && write_fd.count(path)) {
            if (fd == write_fd.at(path)) write_fd.erase(path);
        }
        // m.unlock();
        return;
    }

    void close_file(const char *path, int fd, int mode) {
        close_file(std::string(path), fd, mode);
    }

    // open for write, overload two 
    bool is_open_for_write(std::string path) {
        bool ret = false;
        // m.lock();
        if (open_files.count(path)) ret = write_fd.count(path);
        // m.unlock();
        // else not in map, so not open
        return ret;
    }
    bool is_open_for_write(const char *path) {
        return is_open_for_write(std::string(path));
    }

    // LOCKING FUNCTIONALITY FOR TRANSFERS
    int lock_file(std::string path, rw_lock_mode_t mode) {
        // just need to lock it to atomically add a RWLock
        m.lock();
        if (!file_locks.count(path)) {
            file_locks.insert(
                std::pair<std::string, std::unique_ptr<RWLock>>(
                    path, 
                    std::make_unique<RWLock>()
                )
            );
        }
        m.unlock();

        // NOW can get transfer read write lock
        return file_locks.at(path)->lock(mode);
    }

    int unlock_file(std::string path, rw_lock_mode_t mode) {
        return file_locks.at(path)->release(mode);
    }

    /// overloading for const char
    int lock_file(const char *path, rw_lock_mode_t mode) {
        return lock_file(std::string(path), mode);
    }

    int unlock_file(const char *path, rw_lock_mode_t mode) {
        return unlock_file(std::string(path), mode);
    }
};

// used to keep state on server side
ServerBook filebook{};


///////////////
// ATOMIC FILE TRANSFER

int watdfs_rw_acquire(int *argTypes, void **args) {
    const char *path = (const char*)args[0];
    int *lock_mode = (int*)args[1];
    int *ret = (int*)args[2];

    rw_lock_mode_t mode = (*lock_mode == 0) ? RW_READ_LOCK : RW_WRITE_LOCK; 
    int fn_ret = filebook.lock_file(path, mode);
    *ret = fn_ret;
    return 0;
}

int watdfs_rw_release(int *argTypes, void **args) {
    const char *path = (const char*)args[0];
    int *lock_mode = (int*)args[1];
    int *ret = (int*)args[2];

    rw_lock_mode_t mode = (*lock_mode == 0) ? RW_READ_LOCK : RW_WRITE_LOCK; 
    int fn_ret = filebook.unlock_file(path, mode);
    *ret = fn_ret;
    return 0;
}


// Important: the server needs to handle multiple concurrent client requests.
// You have to be carefuly in handling global variables, esp. for updating them.
// Hint: use locks before you update any global variable.

// We need to operate on the path relative to the the server_persist_dir.
// This function returns a path that appends the given short path to the
// server_persist_dir. The character array is allocated on the heap, therefore
// it should be freed after use.
// Tip: update this function to return a unique_ptr for automatic memory management.
char *get_full_path(char *short_path) {
    int short_path_len = strlen(short_path);
    persist_mut.lock();
    int dir_len = strlen(server_persist_dir);
    int full_len = dir_len + short_path_len + 1;

    char *full_path = (char *)malloc(full_len);

    // First fill in the directory.
    strcpy(full_path, server_persist_dir);
    persist_mut.unlock();
    // Then append the path.
    strcat(full_path, short_path);
    DLOG("Full path: %s\n", full_path);

    return full_path;
}

// The server implementation of getattr.
int watdfs_getattr(int *argTypes, void **args) {
    // Get the arguments.
    // The first argument is the path relative to the mountpoint.
    char *short_path = (char *)args[0];
    // The second argument is the stat structure, which should be filled in
    // by this function.
    struct stat *statbuf = (struct stat *)args[1];
    // The third argument is the return code, which should be set be 0 or -errno.
    int *ret = (int *)args[2];

    // Get the local file name, so we call our helper function which appends
    // the server_persist_dir to the given path.
    char *full_path = get_full_path(short_path);

    // Initially we set set the return code to be 0.
    *ret = 0;

    // Let sys_ret be the return code from the stat system call.
    int sys_ret = 0;

    // TODO: Make the stat system call, which is the corresponding system call needed
    // to support getattr. You should use the statbuf as an argument to the stat system call.
    // (void)statbuf;
    sys_ret = stat(full_path, statbuf);

    if (sys_ret < 0) {
        // If there is an error on the system call, then the return code should
        // be -errno.
        *ret = -errno;
    }

    // Clean up the full path, it was allocated on the heap.
    free(full_path);

    DLOG("Returning code: %d", *ret);
    // The RPC call succeeded, so return 0.
    return 0;
}

int watdfs_mknod(int *argTypes, void **args) {
    PROLOGUE;

    // args[1] is mode
    mode_t *mode = (mode_t *)args[1];

    // args[2] is dev
    dev_t *dev = (dev_t *)args[2];

    // args[3] is retcode
    int *ret = (int *)args[3];
    *ret = 0;

    // actual syscall
    int sys_ret = mknod(full_path, *mode, *dev);

    DLOG("Return val is %d, errno is %d", sys_ret, errno);

    UPDATE_RET;

    EPILOGUE(ret, "watdfs_mknod");
    return 0;
}

int watdfs_open(int *argTypes, void **args) {
    PROLOGUE;

    // args[1] is fi
    struct fuse_file_info *fi = (struct fuse_file_info *)args[1];

    // args[3] is retcode
    int *ret = (int *)args[2];
    *ret = 0;

    bool want_write = (fi->flags & (O_RDWR | O_WRONLY)) != 0; 

    filebook.lock();
    if (want_write && filebook.is_open_for_write(short_path)) {
        filebook.unlock();
        // return ;
        *ret = -EACCES;
        // return 0;
    } else {
        // either not open for write, 
        // or we don't want to open for write
        // file could be open or not -- need to figure out what to do
        int fd;
        if (filebook.is_file_open(short_path)) {
            // just give it the fd that's open
            fd = filebook.get_fd(short_path);
        } else {
            DLOG("in server_open, the old fashioned way.");
            fd = open(full_path, fi->flags);
        }
        if (fd < 0) {
            *ret = -errno;
        } else {
            // if there was an error we dont want to open file 
            // and mess up the count
            filebook.open_file(short_path, fd, fi->flags);
        }
        filebook.unlock();
        // else, fill in file descriptor to fi
        fi->fh = fd;
    }

    EPILOGUE(ret, "watdfs_open");
    return 0;
}

int watdfs_release(int *argTypes, void **args) {
    PROLOGUE;

    // args[1] is fi
    struct fuse_file_info *fi = (struct fuse_file_info *)args[1];

    // args[3] is retcode
    int *ret = (int *)args[2];
    *ret = 0;

    // critical section, make sure that updating this metdata
    // is atomic
    filebook.lock();
    // actual syscall
    filebook.close_file(short_path, fi->fh, fi->flags);

    int sys_ret = 0;

    // if we closed all open files, then actually close the file
    if (!filebook.is_file_open(short_path))
        sys_ret = close(fi->fh);

    filebook.unlock(); // out of critical section

    UPDATE_RET;

    EPILOGUE(ret, "watdfs_release");
    return 0;
}

int watdfs_read(int *argTypes, void **args) {
    // PROLOGUE;

    // args[1]
    char *buf = (char *)args[1];
    // args[2]
    size_t *sz = (size_t *)args[2];
    // args[3]
    off_t *offset = (off_t *)args[3];
    // args[4]
    struct fuse_file_info *fi = (struct fuse_file_info *)args[4];
    // args[5]
    int *ret = (int *)args[5];
    *ret = 0;

    DLOG("in watdfs_read: size is %ld and offset is %ld\n", *sz, *offset);
    // filebook.lock();
    int sys_ret = pread(fi->fh, buf, *sz, *offset);
    // filebook.unlock();
    DLOG("in watdfs_read: sys_ret is %d and errno is %d\n", sys_ret, errno);

    // HANDLE ERRORS
    // UPDATE_RET;
    if (sys_ret >= 0) {
        *ret = sys_ret;
    } else {
        *ret = -errno;
    }

    DLOG("Returning from watdfs_read with code: %d", *ret);
    // EPILOGUE(ret, "watdfs_read");
    return 0;
}

int watdfs_write(int *argTypes, void **args) {
    // PROLOGUE;

    // args[1]
    const char *buf = (const char *)args[1];
    // args[2]
    size_t *sz = (size_t *)args[2];
    // args[3]
    off_t *offset = (off_t *)args[3];
    // args[4]
    struct fuse_file_info *fi = (struct fuse_file_info *)args[4];
    // args[5]
    int *ret = (int *)args[5];
    *ret = 0;

    // filebook.lock();
    int sys_ret = pwrite(fi->fh, buf, *sz, *offset);
    // filebook.unlock();

    DLOG("in watdfs_write: sys_ret is %d and errno is %d\n", sys_ret, errno);

    // HANDLE ERRORS
    // UPDATE_RET;
    if (sys_ret >= 0) {
        *ret = sys_ret;
    } else {
        *ret = -errno;
    }

    DLOG("Returning from watdfs_write with code: %d", *ret);
    // EPILOGUE(ret, "watdfs_write");
    return 0;
}

int watdfs_truncate(int *argTypes, void **args) {
    PROLOGUE;

    // const char *path = (const char *)args[0];
    off_t *newsize = (off_t *)args[1];
    int *ret = (int *)args[2];
    *ret = 0;

    int sys_ret = truncate(full_path, *newsize);

    // HANDLE ERRORS
    UPDATE_RET;

    EPILOGUE(ret, "watdfs_truncate");
    return 0;
}

int watdfs_fsync(int *argTypes, void **args) {
    PROLOGUE;

    struct fuse_file_info *fi = (struct fuse_file_info *)args[1];
    int *ret = (int *)args[2];
    *ret = 0;

    int sys_ret = fsync(fi->fh);

    // HANDLE ERRORS
    UPDATE_RET;

    EPILOGUE(ret, "watdfs_fsync");
    return 0;
}

int watdfs_utimensat(int *argTypes, void **args) {
    PROLOGUE;

    // const char *path = (const char *)args[0];
    const struct timespec *ts = (const struct timespec *)args[1];
    int *ret = (int *)args[2];
    *ret = 0;

    int sys_ret = utimensat(0, full_path, ts, 0);

    UPDATE_RET;

    EPILOGUE(ret, "watdfs_utimensat"); 
    return 0;
}

// The main function of the server.
int main(int argc, char *argv[]) {
    // argv[1] should contain the directory where you should store data on the
    // server. If it is not present it is an error, that we cannot recover from.
    if (argc != 2) {
        // In general you shouldn't print to stderr or stdout, but it may be
        // helpful here for debugging. Important: Make sure you turn off logging
        // prior to submission!
        // See watdfs_client.c for more details
        // # ifdef PRINT_ERR
        // std::cerr << "Usaage:" << argv[0] << " server_persist_dir";
        // #endif
        return -1;
    }
    // Store the directory in a global variable.
    persist_mut.lock();
    server_persist_dir = argv[1];
    persist_mut.unlock();

    // TODO: Initialize the rpc library by calling `rpcServerInit`.
    // Important: `rpcServerInit` prints the 'export SERVER_ADDRESS' and
    // 'export SERVER_PORT' lines. Make sure you *do not* print anything
    // to *stdout* before calling `rpcServerInit`.
    //DLOG("Initializing server...");
    int init_ret = rpcServerInit();
    if (init_ret < 0) {
        DLOG("Error initializing server... Return");
        return 1;
    }

    int ret = 0;

    // rw_lock_init(&rw_lock);
    // TODO: If there is an error with `rpcServerInit`, it maybe useful to have
    // debug-printing here, and then you should return.

    // TODO: Register your functions with the RPC library.
    // Note: The braces are used to limit the scope of `argTypes`, so that you can
    // reuse the variable for multiple registrations. Another way could be to
    // remove the braces and use `argTypes0`, `argTypes1`, etc.
    {
        // There are 3 args for the function (see watdfs_client.c for more
        // detail).
        int argTypes[4];
        // First is the path.
        argTypes[0] =
            (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        // The second argument is the statbuf.
        argTypes[1] =
            (1u << ARG_OUTPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        // The third argument is the retcode.
        argTypes[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        // Finally we fill in the null terminator.
        argTypes[3] = 0;

        // We need to register the function with the types and the name.
        ret = rpcRegister((char *)"getattr", argTypes, watdfs_getattr);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    // mknod
    {
        SETUP_SERVER_ARG(4);

        // mode
        arg_types[1] = encode_arg_type(true, false, false, ARG_INT, 0);
        // dev
        arg_types[2] = encode_arg_type(true, false, false, ARG_LONG, 0);

        // rpc register
        ret = RPC_REG("mknod", watdfs_mknod);

        if (ret < 0) return ret;
    }

    // open
    {
        SETUP_SERVER_ARG(3);

        // fi
        arg_types[1] = encode_arg_type(true, true, true, ARG_CHAR, 1u);

        ret = RPC_REG("open", watdfs_open);

        if (ret < 0) return ret;
    }

    // release
    {
        SETUP_SERVER_ARG(3);
        
        // fi
        arg_types[1] = encode_arg_type(true, false, true, ARG_CHAR, 1u);

        ret = RPC_REG("release", watdfs_release);

        if (ret < 0) return ret;
    }

    // read 
    {
        SETUP_SERVER_ARG(6);
        arg_types[1] = encode_arg_type(false, true, true, ARG_CHAR, 1u);
        arg_types[2] = encode_arg_type(true, false, false, ARG_LONG, 0);
        arg_types[3] = encode_arg_type(true, false, false, ARG_LONG, 0);
        arg_types[4] = encode_arg_type(true, false, true, ARG_CHAR, 1u);

        ret = RPC_REG("read", watdfs_read);

        if (ret < 0) return ret;
    }

    // write 
    {
        SETUP_SERVER_ARG(6);
        arg_types[1] = encode_arg_type(true, false, true, ARG_CHAR, 1u);
        arg_types[2] = encode_arg_type(true, false, false, ARG_LONG, 0);
        arg_types[3] = encode_arg_type(true, false, false, ARG_LONG, 0);
        arg_types[4] = encode_arg_type(true, false, true, ARG_CHAR, 1u);

        ret = RPC_REG("write", watdfs_write);

        if (ret < 0) return ret;
    }

    // truncate
    {
        SETUP_SERVER_ARG(3);
        arg_types[1] = encode_arg_type(true, false, false, ARG_LONG, 0);

        ret = RPC_REG("truncate", watdfs_truncate);

        if (ret < 0) return ret;
    }

    // fsync
    {
        SETUP_SERVER_ARG(3);
        arg_types[1] = encode_arg_type(true, false, true, ARG_CHAR, 1u);

        ret = RPC_REG("fsync", watdfs_fsync);

        if (ret < 0) return ret;
    }

    // utimensat
    {
        SETUP_SERVER_ARG(3);
        arg_types[1] = encode_arg_type(true, false, true, ARG_CHAR, 1u);

        ret = RPC_REG("utimensat", watdfs_utimensat);

        if (ret < 0) return ret;
    }

    // get_rw_lock
    {
        int arg_types[3];
        arg_types[0] = encode_arg_type(true, false, false, ARG_INT, 0);
        arg_types[1] = encode_retcode();
        arg_types[2] = 0; // null terminated

        int ret = RPC_REG("get_rw_lock", watdfs_rw_acquire);
    }

    // release_rw_lock
    {
        int arg_types[3];
        arg_types[0] = encode_arg_type(true, false, false, ARG_INT, 0);
        arg_types[1] = encode_retcode();
        arg_types[2] = 0; // null terminated

        int ret = RPC_REG("release_rw_lock", watdfs_rw_release);
    }

    // TODO: Hand over control to the RPC library by calling `rpcExecute`.
    int exec_ret = rpcExecute();

    // rw_lock_destroy(&rw_lock);

    if (exec_ret < 0) {
        DLOG("error with rpcExecute...");
        return 1;
    }

    // rpcExecute could fail so you may want to have debug-printing here, and
    // then you should return.
    return ret;
}
