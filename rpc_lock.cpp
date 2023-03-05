#include "rpc_lock.h"
#include "watdfs_make_args.h"
#include "rpc.h"
#include <string.h>
#include <fcntl.h>           /* Definition of AT_* constants */
#include <sys/stat.h>
#include "a2_client.h"

// FOR ATOMIC FILE TRANSFERS
int watdfs_get_rw_lock(const char *path, bool is_write) {
    // will return 0 if you get the lock
    void **args = new void*[3];
    int arg_types[4];

    // need path because we'll do it by file
    int pathlen = strlen(path) + 1;
    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint)pathlen;
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