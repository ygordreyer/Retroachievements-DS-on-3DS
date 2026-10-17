#ifndef RA_QUEUE_IO_H
#define RA_QUEUE_IO_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* read returns bytes, -1 absent, -2 I/O error; write flushes, syncs and closes.
 * inplace preserves an existing target's cluster chain; prefix precedes payload. */
typedef struct raQueueFileOps {
    void* context;
    int (*read)(void*, const char*, void*, size_t, void*, size_t);
    int (*write)(void*, const char*, const void*, size_t, const void*, size_t, int);
    int (*remove)(void*, const char*);
} raQueueFileOps;

int raQueueRecover(const char* path, void* scratch, size_t size, const raQueueFileOps* ops);
int raQueueReplace(const char* path, const void* data, size_t size, const raQueueFileOps* ops);
int raQueueBootRecover(const char* path);
int raQueueAvailable(const char* path);
extern const raQueueFileOps raQueueStdFileOps;
#ifdef __cplusplus
}
#endif
#endif
