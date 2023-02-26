#include <string.h>
#include <sys/types.h>
#include "debug.h"
#include "watdfs_client.h"

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

namespace a2 {
    int watdfs_cli_getattr(void *userdata, const char *path, struct stat *statbuf);
    int watdfs_cli_mknod(void *userdata, const char *path, mode_t mode, dev_t dev);
    int watdfs_cli_open(void *userdata, const char *path,struct fuse_file_info *fi);
    int watdfs_cli_release(void *userdata, const char *path, struct fuse_file_info *fi);
    int watdfs_read_write_single(
        void *userdata, 
        const char *path, 
        char *buf, 
        size_t size,
        off_t offset, 
        struct fuse_file_info *fi,
        bool is_read
    );
    int watdfs_read_write_full(
        void *userdata,
        const char *path,
        char *buf, 
        size_t size,
        off_t offset,
        struct fuse_file_info *fi,
        bool is_read
    );
    int watdfs_cli_read(void *userdata, const char *path, char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi);
    int watdfs_cli_write(void *userdata, const char *path, const char *buf,
                         size_t size, off_t offset, struct fuse_file_info *fi);
    int watdfs_cli_truncate(void *userdata, const char *path, off_t newsize);
    int watdfs_cli_fsync(void *userdata, const char *path,
                         struct fuse_file_info *fi);
    int watdfs_cli_utimensat(void *userdata, const char *path,
                             const struct timespec ts[2]);
}