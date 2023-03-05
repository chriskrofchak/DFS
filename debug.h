#ifndef DEBUG_H
#define DEBUG_H

#ifdef NDEBUG

// disable functions
#define DLOG(...) ((void)0)

#define INIT_LOG

#else

#include <pthread.h>
#include <stdio.h>

extern pthread_mutex_t __nfs_debug_lock__;

#define INIT_LOG pthread_mutex_t __nfs_debug_lock__ = PTHREAD_MUTEX_INITIALIZER;

#define DLOG(fmt, ...)                                                         \
    do {                                                                       \
        pthread_mutex_lock(&__nfs_debug_lock__);                               \
        fprintf(stderr, "DEBUG %lu [%s:%d] ", pthread_self(), __FILE__,         \
                __LINE__);                                                     \
        fprintf(stderr, fmt, ##__VA_ARGS__);                                   \
        fprintf(stderr, "\n");                                                 \
        pthread_mutex_unlock(&__nfs_debug_lock__);                             \
    } while (0)
#endif // NDEBUG

// OTHER DEBUG MACROS

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

#endif
