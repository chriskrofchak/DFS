#ifndef RPC_LOCK_H
#define RPC_LOCK_H

int watdfs_get_rw_lock(const char *path, bool is_write);
int watdfs_release_rw_lock(const char *path, bool is_write);

#endif 