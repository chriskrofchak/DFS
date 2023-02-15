//
// Starter code for CS 454/654
// You SHOULD change this file
//

#include "watdfs_client.h"
#include "watdfs_make_args.h"
#include "debug.h"
INIT_LOG

#include "rpc.h"
#include <algorithm>


/////////////////////////
// BOILER PLATE
// makes arg_types, 
// makes args
// makes retcode
// sets [0] to path
// and [ARG_COUNT - 1] to retcode
// and null terminates arg_types
////////////////////////

#define MAKE_CLIENT_ARGS(_ARG_COUNT, _PATH) \
    MAKE_ARGS(_ARG_COUNT, _PATH);           \
    MAKE_ARG_TYPES(_ARG_COUNT);             \
    SETUP_ARG_TYPES(_ARG_COUNT, _PATH) 

#define VOIDIFY(element) (void *)element

#define RPCIFY(fn_string) rpcCall((char *)fn_string, arg_types, args)

#define FREE_ARGS() delete []args




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
    // SET UP THE RPC CALL
    DLOG("watdfs_cli_getattr called for '%s'", path);
    
    // getattr has 3 arguments.
    int ARG_COUNT = 3;

    // Allocate space for the output arguments.
    void **args = new void*[ARG_COUNT];

    // Allocate the space for arg types, and one extra space for the null
    // array element.
    int arg_types[ARG_COUNT + 1];

    // The path has string length (strlen) + 1 (for the null character).
    int pathlen = strlen(path) + 1;

    // Fill in the arguments
    // The first argument is the path, it is an input only argument, and a char
    // array. The length of the array is the length of the path.
    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    // For arrays the argument is the array pointer, not a pointer to a pointer.
    args[0] = (void *)path;

    // The second argument is the stat structure. This argument is an output
    // only argument, and we treat it as a char array. The length of the array
    // is the size of the stat structure, which we can determine with sizeof.
    arg_types[1] = (1u << ARG_OUTPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) |
                   (uint) sizeof(struct stat); // statbuf
    args[1] = (void *)statbuf;

    // The third argument is the return code, an output only argument, which is
    // an integer.
    // TODO: fill in this argument type.
    arg_types[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);

    // The return code is not an array, so we need to hand args[2] an int*.
    // The int* could be the address of an integer located on the stack, or use
    // a heap allocated integer, in which case it should be freed.
    // TODO: Fill in the argument
    int retcode = 0;
    args[2] = (void *)&retcode;

    // Finally, the last position of the arg types is 0. There is no
    // corresponding arg.
    arg_types[3] = 0;

    // MAKE THE RPC CALL
    int rpc_ret = rpcCall((char *)"getattr", arg_types, args);

    // HANDLE THE RETURN
    // The integer value watdfs_cli_getattr will return.
    int fxn_ret = 0;
    if (rpc_ret < 0) {
        DLOG("getattr rpc failed with error '%d'", rpc_ret);
        // Something went wrong with the rpcCall, return a sensible return
        // value. In this case lets return, -EINVAL
        fxn_ret = -EINVAL;
    } else {
        // Our RPC call succeeded. However, it's possible that the return code
        // from the server is not 0, that is it may be -errno. Therefore, we
        // should set our function return value to the retcode from the server.

        // TODO: set the function return value to the return code from the server.
        fxn_ret = retcode;
    }

    if (fxn_ret < 0) {
        // If the return code of watdfs_cli_getattr is negative (an error), then 
        // we need to make sure that the stat structure is filled with 0s. Otherwise,
        // FUSE will be confused by the contradicting return values.
        memset(statbuf, 0, sizeof(struct stat));
    }

    // Clean up the memory we have allocated.
    delete []args;

    // Finally return the value we got from the server.
    return fxn_ret;
}

// CREATE, OPEN AND CLOSE
int watdfs_cli_mknod(void *userdata, const char *path, mode_t mode, dev_t dev) {
    // Called to create a file.

    // boilerplate
    MAKE_CLIENT_ARGS(4, path);

    // mode
    arg_types[1] = encode_arg_type(true, false, false, ARG_INT, 0);
    args[1] = VOIDIFY(&mode);

    // dev
    arg_types[2] = encode_arg_type(true, false, false, ARG_LONG, 0);
    args[2] = VOIDIFY(&dev);

    // make rpc call
    int rpc_ret = RPCIFY("mknod");
    int fxn_ret = 0;

    if (rpc_ret < 0) {
        // handle errors
        DLOG("mknod rpc failed with error '%d'", rpc_ret);
        // Something went wrong with the rpcCall, return a sensible return
        // value. In this case lets return, -EINVAL
        fxn_ret = -EINVAL;
    } else {
        // fine!
        fxn_ret = retcode;
    }

    // FREE boilerplate at end
    FREE_ARGS();
    return fxn_ret;
}
int watdfs_cli_open(void *userdata, const char *path,
                    struct fuse_file_info *fi) {
    // Called during open.
    // You should fill in fi->fh.
    MAKE_CLIENT_ARGS(3, path);

    // fi arg
    arg_types[1] = encode_arg_type(true, true, true, ARG_CHAR, (uint)sizeof(struct fuse_file_info));
    args[1] = VOIDIFY(fi);

    int rpc_ret = RPCIFY("open");
    int fxn_ret = 0;

    if (rpc_ret < 0) {
        // handle errors
        DLOG("open rpc failed with error '%d'", rpc_ret);
        // Something went wrong with the rpcCall, return a sensible return
        // value. In this case lets return, -EINVAL
        fxn_ret = -EINVAL;
    } else {
        // fine!
        fxn_ret = retcode;
    }

    // FREE boilerplate at end
    FREE_ARGS();
    return fxn_ret;
}

int watdfs_cli_release(void *userdata, const char *path,
                       struct fuse_file_info *fi) {
    // Called during close, but possibly asynchronously.
    // Called during open.
    // You should fill in fi->fh.
    MAKE_CLIENT_ARGS(3, path);

    // fi arg
    arg_types[1] = encode_arg_type(true, false, true, ARG_CHAR, (uint)sizeof(struct fuse_file_info));
    args[1] = VOIDIFY(fi);

    int rpc_ret = RPCIFY("release");
    int fxn_ret = 0;

    if (rpc_ret < 0) {
        // handle errors
        DLOG("release rpc failed with error '%d'", rpc_ret);
        // Something went wrong with the rpcCall, return a sensible return
        // value. In this case lets return, -EINVAL
        fxn_ret = -EINVAL;
    } else {
        // fine!
        fxn_ret = retcode;
    }

    // FREE boilerplate at end
    FREE_ARGS();
    return fxn_ret;
}

int watdfs_read_write_single(
    void *userdata, 
    const char *path, 
    char *buf, 
    size_t size,
    off_t offset, 
    struct fuse_file_info *fi,
    bool is_read)
{
    // put it here
    // size < MAX_ARRAY_LEN
    MAKE_CLIENT_ARGS(6, path);

    // buf
    if (is_read) {
        arg_types[1] = encode_arg_type(false, true, true, ARG_CHAR, size);
    } else {
        arg_types[1] = encode_arg_type(true, false, true, ARG_CHAR, size);
    }
    args[1] = VOIDIFY(buf);

    // size
    arg_types[2] = encode_arg_type(true, false, false, ARG_LONG, 0);
    args[2] = VOIDIFY(&size);

    // offset
    arg_types[3] = encode_arg_type(true, false, false, ARG_LONG, 0);
    args[3] = VOIDIFY(&offset);

    // fi
    arg_types[4] = encode_arg_type(true, false, true, ARG_CHAR, (uint)sizeof(struct fuse_file_info));
    args[4] = VOIDIFY(fi);

    int rpc_ret = 0;
    if (is_read) {
        rpc_ret = RPCIFY("read");
    } else {
        rpc_ret = RPCIFY("write");
    } 

    int fxn_ret = 0;

    if (rpc_ret < 0) {
        // handle errors
        if (is_read)
            DLOG("read rpc failed with error '%d'", rpc_ret);
        else
            DLOG("write rpc failed with error '%d'", rpc_ret);
        // Something went wrong with the rpcCall, return a sensible return
        // value. In this case lets return, -EINVAL
        fxn_ret = -EINVAL;
    } else {
        if (is_read)
            DLOG("read operation itself failed with err code %d", retcode);
        else
            DLOG("write operation itself failed with err code %d", retcode);
        
        // fine!
        fxn_ret = retcode;
    }

    // FREE boilerplate at end
    FREE_ARGS();
    return fxn_ret;
}

// uses "read" but is really either read or write
int watdfs_read_write_full(
    void *userdata,
    const char *path,
    char *buf, 
    size_t size,
    off_t offset,
    struct fuse_file_info *fi,
    bool is_read
)
{
    size_t temp_left_to_read = size;
    off_t temp_offset = offset;
    char * temp_buf = buf;
    int rpc_ret, fxn_ret;
    fxn_ret = 0;
    while (temp_left_to_read > 0) {
        size_t to_read = std::min((size_t)MAX_ARRAY_LEN, temp_left_to_read);
        DLOG("to_read, temp_offset is: %ld, %ld", to_read, temp_offset);
        rpc_ret = watdfs_read_write_single(userdata, path, temp_buf, to_read, temp_offset, fi, is_read);
        
        // error handling
        if (rpc_ret < 0) {
            DLOG("one of reads or writes went wrong, carrying up the error...");
            return -EINVAL;
        } else {
            // good! add this to fxn_ret
            fxn_ret += rpc_ret;
            DLOG("read or write iterated correctly, currently reading %d bytes", fxn_ret);
        }

        // increment as we loop
        temp_left_to_read -= to_read;
        temp_buf += to_read;
        temp_offset += (off_t)to_read;
    }
    return fxn_ret;
}

// READ AND WRITE DATA
int watdfs_cli_read(void *userdata, const char *path, char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    // Read size amount of data at offset of file into buf.
    // Remember that size may be greater then the maximum array size of the RPC
    // library.

    return watdfs_read_write_full(userdata, path, buf, size, offset, fi, true);
}

int watdfs_cli_write(void *userdata, const char *path, const char *buf,
                     size_t size, off_t offset, struct fuse_file_info *fi) {
    // Write size amount of data at offset of file from buf.

    // Remember that size may be greater then the maximum array size of the RPC
    // library.
    return watdfs_read_write_full(userdata, path, (char *)buf, size, offset, fi, false);
}

int watdfs_cli_truncate(void *userdata, const char *path, off_t newsize) {
    // Change the file size to newsize.
    MAKE_CLIENT_ARGS(3, path);

    // newsize
    arg_types[1] = encode_arg_type(true, false, false, ARG_LONG, 0);
    args[1] = VOIDIFY(&newsize);

    // call rpc 
    int rpc_ret = RPCIFY("truncate");
    int fxn_ret = 0;

    if (rpc_ret < 0) {
        // handle errors
        DLOG("truncate rpc failed with error '%d'", rpc_ret);
        // Something went wrong with the rpcCall, return a sensible return
        // value. In this case lets return, -EINVAL
        fxn_ret = -EINVAL;
    } else {
        // fine!
        DLOG("truncate call itself failed with error '%d'", retcode);
        fxn_ret = retcode;
    }

    // FREE boilerplate at end
    FREE_ARGS();
    return fxn_ret;
}

int watdfs_cli_fsync(void *userdata, const char *path,
                     struct fuse_file_info *fi) {
    // Force a flush of file data.
    MAKE_CLIENT_ARGS(3, path);

    // newsize
    arg_types[1] = encode_arg_type(true, false, true, ARG_CHAR, (uint)sizeof(struct fuse_file_info));
    args[1] = VOIDIFY(fi);

    // call rpc 
    int rpc_ret = RPCIFY("fsync");
    int fxn_ret = 0;

    if (rpc_ret < 0) {
        // handle errors
        DLOG("fsync rpc failed with error '%d'", rpc_ret);
        // Something went wrong with the rpcCall, return a sensible return
        // value. In this case lets return, -EINVAL
        fxn_ret = -EINVAL;
    } else {
        // fine!
        DLOG("fsync call itself failed with error '%d'", retcode);
        fxn_ret = retcode;
    }

    // FREE boilerplate at end
    FREE_ARGS();
    return fxn_ret;
}

// CHANGE METADATA
int watdfs_cli_utimensat(void *userdata, const char *path,
                       const struct timespec ts[2]) {
    // Change file access and modification times.
    MAKE_CLIENT_ARGS(3, path);

    // newsize
    arg_types[1] = encode_arg_type(true, false, true, ARG_CHAR, (uint)2*sizeof(struct timespec));
    args[1] = VOIDIFY(&ts);

    // call rpc 
    int rpc_ret = RPCIFY("utimensat");
    int fxn_ret = 0;

    if (rpc_ret < 0) {
        // handle errors
        DLOG("utimensat rpc failed with error '%d'", rpc_ret);
        // Something went wrong with the rpcCall, return a sensible return
        // value. In this case lets return, -EINVAL
        fxn_ret = -EINVAL;
    } else {
        // fine!
        DLOG("utimensat call itself failed with error '%d'", retcode);
        fxn_ret = retcode;
    }

    // FREE boilerplate at end
    FREE_ARGS();
    return fxn_ret;
}
