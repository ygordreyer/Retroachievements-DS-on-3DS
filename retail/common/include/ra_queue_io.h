#ifndef RA_QUEUE_IO_H
#define RA_QUEUE_IO_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* read returns bytes, -1 absent, -2 I/O error; write flushes, syncs and closes.
 * inplace scrubs a shrinking tail before truncation and preserves an existing target's cluster chain; prefix precedes payload. */
typedef struct raQueueFileOps {
    void* context;
    int (*read)(void*, const char*, void*, size_t, void*, size_t);
    int (*write)(void*, const char*, const void*, size_t, const void*, size_t, int);
    int (*remove)(void*, const char*);
} raQueueFileOps;

#define RA_CFG_FILE_MAX 16384u

/* Recover before replacing. Capacity bounds variable-length replay before any target write. */
int raFileRecover(const char* path, void* scratch, size_t capacity, const raQueueFileOps* ops);
int raFileReplace(const char* path, const void* data, size_t size, const raQueueFileOps* ops);
int raConfigRecover(const char* path);

int raQueueRecover(const char* path, void* scratch, size_t size, const raQueueFileOps* ops);
int raQueueReplace(const char* path, const void* data, size_t size, const raQueueFileOps* ops);
int raQueueBootRecover(const char* path);
int raQueueAvailable(const char* path);
/* Resize recognized records through the journal; preserve unknown files and disable appends. */
int raQueuePrepare(const char* path, const raQueueFileOps* ops);
extern const raQueueFileOps raQueueStdFileOps;
#ifdef __cplusplus
}
#endif
#endif
