#include "ra_wifi.h"
#include "ra_queue_io.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define RA_QUEUE_JOURNAL_MAGIC 0x314A4152u
#define RA_QUEUE_JOURNAL_VERSION 1u
#define RA_QUEUE_PATH_MAX 80

typedef struct raQueueJournal {
    uint32_t magic, version, size, checksum;
} raQueueJournal;

static unsigned char bootState[2], bootTried[2];

static unsigned queueDevice(const char* path) {
    return strncmp(path, "fat:/", 5) == 0;
}

int raQueueAvailable(const char* path) {
    return bootState[queueDevice(path)] != 2;
}

static uint32_t queueChecksum(const void* data, size_t size) {
    const unsigned char* p = data;
    uint32_t hash = 2166136261u;
    while (size--) hash = (hash ^ *p++) * 16777619u;
    return hash;
}

static int queuePaths(const char* path, char* temp, char* journal) {
    if (strlen(path) + 5 >= RA_QUEUE_PATH_MAX) return 0;
    strcpy(temp, path);
    strcat(temp, ".new");
    strcpy(journal, path);
    strcat(journal, ".txn");
    return 1;
}

static int queueJournalRemove(const char* journal, size_t size, int scrub, const raQueueFileOps* ops) {
    if (scrub && size) {
        void* zeros = calloc(1, size);
        int ok;
        if (!zeros) return 0;
        ok = ops->write(ops->context, journal, 0, 0, zeros, size, 1);
        free(zeros);
        if (!ok) return 0;
    }
    return ops->remove(ops->context, journal);
}

static int queueRecover(const char* path, void* scratch, size_t size, size_t required, const raQueueFileOps* ops) {
    char temp[RA_QUEUE_PATH_MAX], journal[RA_QUEUE_PATH_MAX];
    raQueueJournal mark;
    int got;
    if (!queuePaths(path, temp, journal)) return 0;
    if (!ops->remove(ops->context, temp)) return 0;
    got = ops->read(ops->context, journal, &mark, sizeof(mark), scratch, size);
    if (got == -1) return 1;
    if (got == -2) return 0;
    if (got < (int)sizeof(mark) || mark.size > size
     || (required && mark.size != required)
     || got != (int)(sizeof(mark) + mark.size) || mark.magic != RA_QUEUE_JOURNAL_MAGIC
     || mark.version != RA_QUEUE_JOURNAL_VERSION
     || queueChecksum(scratch, mark.size) != mark.checksum)
        return (required || got <= (int)(sizeof(mark) + size))
            && queueJournalRemove(journal, (size_t)got, !required, ops);
    /* A valid redo remains until the target has been synced and closed. */
    if (!ops->write(ops->context, path, 0, 0, scratch, mark.size, 1)) return 0;
    return queueJournalRemove(journal, (size_t)got, !required, ops);
}

int raQueueRecover(const char* path, void* scratch, size_t size, const raQueueFileOps* ops) {
    int ok = queueRecover(path, scratch, size, size, ops);
    if (ok) bootState[queueDevice(path)] = 1;
    return ok;
}

int raQueueBootRecover(const char* path) {
    unsigned device = queueDevice(path);
    char scratch[RA_QUEUE_BYTES];
    if (!bootTried[device]) {
        bootTried[device] = 1;
        bootState[device] = raQueueRecover(path, scratch, sizeof(scratch), &raQueueStdFileOps) ? 1 : 2;
    }
    return raQueueAvailable(path);
}

int raFileRecover(const char* path, void* scratch, size_t capacity, const raQueueFileOps* ops) {
    return queueRecover(path, scratch, capacity, 0, ops);
}

int raConfigRecover(const char* path) {
    void* scratch = malloc(RA_CFG_FILE_MAX);
    int ok;
    if (!scratch) return 0;
    ok = raFileRecover(path, scratch, RA_CFG_FILE_MAX, &raQueueStdFileOps);
    raSecureClear(scratch, RA_CFG_FILE_MAX);
    free(scratch);
    return ok;
}

static int queueReplace(const char* path, const void* data, size_t size, int scrub, const raQueueFileOps* ops) {
    char temp[RA_QUEUE_PATH_MAX], journal[RA_QUEUE_PATH_MAX];
    raQueueJournal mark;
    if (size > UINT32_MAX || !queuePaths(path, temp, journal)) return 0;
    mark.magic = RA_QUEUE_JOURNAL_MAGIC;
    mark.version = RA_QUEUE_JOURNAL_VERSION;
    mark.size = (uint32_t)size;
    mark.checksum = queueChecksum(data, size);
    return ops->write(ops->context, journal, &mark, sizeof(mark), data, size, 0)
        && ops->write(ops->context, path, 0, 0, data, size, 1)
        && queueJournalRemove(journal, sizeof(mark) + size, scrub, ops);
}

int raFileReplace(const char* path, const void* data, size_t size, const raQueueFileOps* ops) {
    return queueReplace(path, data, size, 1, ops);
}

int raQueueReplace(const char* path, const void* data, size_t size, const raQueueFileOps* ops) {
    if (!raQueueAvailable(path)) return 0;
    if (queueReplace(path, data, size, 0, ops)) return 1;
    /* No game may append behind a redo that will replay on the next boot. */
    bootState[queueDevice(path)] = 2;
    return 0;
}

/* Preserve record bytes, including NUL-padded fields and terminal refusal marks. */
static int queueResize(const unsigned char* input, size_t size, unsigned char* output) {
    size_t at = 0;
    const size_t slotWidth = size == 16 * RA_QUEUE_MAX ? 16
        : size == 32 * RA_QUEUE_MAX ? 32 : size % RA_QUEUE_RECORD == 0 ? RA_QUEUE_RECORD : 0;
    unsigned count = 0;
    while (at < size) {
        size_t start, end, digits, limit;
        uint32_t id = 0;
        while (at < size && input[at] == 0) ++at;
        if (at == size) break;
        start = at;
        if (slotWidth && start % slotWidth) return 0;
        limit = slotWidth ? start + slotWidth : size;
        if (limit > start + RA_QUEUE_RECORD) limit = start + RA_QUEUE_RECORD;
        while (at < limit && input[at] >= '0' && input[at] <= '9') {
            unsigned digit = input[at++] - '0';
            if (id > (UINT32_MAX - digit) / 10) return 0;
            id = id * 10 + digit;
        }
        digits = at - start;
        if (!digits || !id || at == limit || count == RA_QUEUE_MAX) return 0;
        if (input[at] == '\t') {
            ++at;
            for (unsigned i = 0; i < RA_QUEUE_STAMP; ++i)
                if (at == limit || input[at] < '0' || input[at++] > '9') return 0;
            /* Unknown metadata stays byte-for-byte intact within its record. */
            while (at < limit && input[at] != '\n') ++at;
        }
        if (at == limit || input[at] != '\n') return 0;
        end = ++at;
        memcpy(output + count * RA_QUEUE_RECORD, input + start, end - start);
        if (slotWidth) {
            size_t tail = end;
            while (tail < limit && input[tail] == 0) ++tail;
            if (tail == limit - 1 && input[tail] == '!') {
                output[(count + 1) * RA_QUEUE_RECORD - 1] = '!';
                ++tail;
            }
            if (tail != limit) return 0;
            at = limit;
        }
        ++count;
    }
    return 1;
}

int raQueuePrepare(const char* path, const raQueueFileOps* ops) {
    unsigned char* input;
    unsigned char output[RA_QUEUE_BYTES] = {0};
    int got, ok = 0;
    if (!raQueueAvailable(path)) return 0;
    input = malloc(RA_CFG_FILE_MAX);
    if (input) {
        got = ops->read(ops->context, path, 0, 0, input, RA_CFG_FILE_MAX);
        if (got == RA_QUEUE_BYTES) ok = 1;
        else if (got == -1 || (got >= 0 && got <= (int)RA_CFG_FILE_MAX
                          && queueResize(input, (size_t)got, output)))
            ok = raQueueReplace(path, output, sizeof(output), ops);
        free(input);
    }
    if (!ok) bootState[queueDevice(path)] = 2;
    return ok;
}

static int queueRead(void* context, const char* path, void* prefix, size_t prefixSize,
                     void* data, size_t size) {
    FILE* f = fopen(path, "rb");
    size_t got;
    char buffer[512];
    int bad;
    (void)context;
    if (!f) return errno == ENOENT ? -1 : -2;
    if (setvbuf(f, buffer, _IOFBF, sizeof(buffer))) { fclose(f); return -2; }
    got = prefixSize ? fread(prefix, 1, prefixSize, f) : 0;
    if (got == prefixSize) got += fread(data, 1, size, f);
    if (got == prefixSize + size && fgetc(f) != EOF) ++got;
    bad = ferror(f);
    if (fclose(f)) bad = 1;
    raSecureClear(buffer, sizeof(buffer));
    return bad ? -2 : (int)got;
}
static int queueWrite(void* context, const char* path, const void* prefix, size_t prefixSize,
                      const void* data, size_t size, int inplace) {
    FILE* f = fopen(path, inplace ? "r+b" : "wb");
    int ok = 1;
    long oldSize = 0;
    size_t total = prefixSize + size;
    char buffer[512], zeros[256] = {0};
    (void)context;
    if (!f && inplace && errno == ENOENT) f = fopen(path, "wb");
    if (!f) return 0;
    if (setvbuf(f, buffer, _IOFBF, sizeof(buffer))) { fclose(f); return 0; }
    if (inplace) {
        ok = !fseek(f, 0, SEEK_END) && (oldSize = ftell(f)) >= 0 && !fseek(f, 0, SEEK_SET);
    }
    if (ok) ok = (!prefixSize || fwrite(prefix, 1, prefixSize, f) == prefixSize)
              && fwrite(data, 1, size, f) == size;
    if (ok && inplace && (size_t)oldSize > total) {
        size_t left = (size_t)oldSize - total;
        while (ok && left) {
            size_t n = left < sizeof(zeros) ? left : sizeof(zeros);
            ok = fwrite(zeros, 1, n, f) == n;
            left -= n;
        }
        if (fflush(f)) ok = 0;
        if (ok && fsync(fileno(f))) ok = 0;
    }
    if (fflush(f)) ok = 0;
    if (ok && inplace && ftruncate(fileno(f), total)) ok = 0;
    if (ok && fsync(fileno(f))) ok = 0;
    if (fclose(f)) ok = 0;
    raSecureClear(buffer, sizeof(buffer));
    return ok;
}
static int queueRemove(void* context, const char* path) {
    (void)context;
    return remove(path) == 0 || errno == ENOENT;
}
const raQueueFileOps raQueueStdFileOps = {
    0, queueRead, queueWrite, queueRemove
};
