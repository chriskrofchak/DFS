#ifndef UPLOAD_H
#define UPLOAD_H

#include <string>

int transfer_file(void *userdata, const char *path, bool persist_fd, struct fuse_file_info *fi);
int watdfs_server_flush_file(void *userdata, const char *path, struct fuse_file_info *fi);
int fresh_fetch(void *userdata, const char *path, struct fuse_file_info *fi);
int fresh_flush(void *userdata, const char *path, struct fuse_file_info *fi);
 
bool watdfs_cli_file_exists(const char *path);
bool operator==(const timespec &l, const timespec &r);

std::string absolut_path(const char *path);

#endif