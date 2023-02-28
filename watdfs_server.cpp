//
// Starter code for CS 454/654
// You SHOULD change this file
//
#include "watdfs_make_args.h"
#include "rpc.h"
#include "debug.h"

INIT_LOG

#include <fuse.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <cstdlib>
#include <mutex>

// Global state server_persist_dir.
char *server_persist_dir = nullptr;
std::mutex persist_mut{};

#define PROLOGUE                                   \
    char *short_path = (char *)args[0];            \
    char *full_path  = get_full_path(short_path)

#define SETUP_SERVER_ARG(__ARG_COUNT) \
    MAKE_ARG_TYPES(__ARG_COUNT); SETUP_ARG_TYPES(__ARG_COUNT, "1") 

#define EPILOGUE(ret, fn) free(full_path); DLOG("Returning from %s with code: %d", fn, *ret)

#define RPC_REG(fn_string, fn_name) rpcRegister((char *)fn_string, arg_types, fn_name)

#define UPDATE_RET if (sys_ret < 0) *ret = -errno

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

    // actual syscall
    int fd = open(full_path, fi->flags);

    if (fd < 0) *ret = -errno;

    // else, fill in file descriptor to fi
    fi->fh = fd;

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

    // actual syscall
    int sys_ret = close(fi->fh);

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
    int sys_ret = pread(fi->fh, buf, *sz, *offset);
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

    int sys_ret = pwrite(fi->fh, buf, *sz, *offset);
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

    // TODO: Hand over control to the RPC library by calling `rpcExecute`.
    int exec_ret = rpcExecute();

    if (exec_ret < 0) {
        DLOG("error with rpcExecute...");
        return 1;
    }

    // rpcExecute could fail so you may want to have debug-printing here, and
    // then you should return.
    return ret;
}
