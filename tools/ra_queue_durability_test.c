#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "ra_wifi.h"
#include "ra_queue_io.h"
#define RA_NET_HOST_TEST
#define sniprintf snprintf
#include "../retail/arm9/source/ra_net.c"

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static const char path[] = "sd:/ra_unlocks.txt";
static const char temp[] = "sd:/ra_unlocks.txt.new";
static const char journal[] = "sd:/ra_unlocks.txt.txn";
typedef struct Image {
    int exists, size, cluster;
    char data[RA_QUEUE_BYTES + 32];
} Image;
static Image files[3];
static int step, crash, ioError, shortWrite, targetError, writes, targetWrites;
static jmp_buf reboot;
static void checkpoint(void) { if (++step == crash) longjmp(reboot, 1); }
static Image* find(const char* name) {
    if (!strcmp(name, path)) return &files[0];
    if (!strcmp(name, temp)) return &files[1];
    CHECK(!strcmp(name, journal));
    return &files[2];
}
static int readFile(void* ctx, const char* name, void* prefix, size_t prefixSize,
                    void* data, size_t size) {
    Image* f = find(name);
    size_t total = size + prefixSize;
    size_t n;
    (void)ctx;
    if (ioError) return -2;
    if (!f->exists) return -1;
    n = f->size < (int)total ? f->size : total;
    size_t head = n < prefixSize ? n : prefixSize;
    if (head) memcpy(prefix, f->data, head);
    if (n > head) memcpy(data, f->data + head, n - head);
    checkpoint(); /* read close */
    return f->size > (int)total ? (int)total + 1 : (int)n;
}
static void writePart(Image* f, int at, const void* data, size_t size) {
    if (size) memcpy(f->data + at, data, size);
    if (f->size < at + (int)size) f->size = at + size;
    checkpoint();
}
static int writeFile(void* ctx, const char* name, const void* prefix, size_t prefixSize,
                     const void* data, size_t size, int inplace) {
    Image* f = find(name);
    (void)ctx;
    if (ioError || (inplace && targetError)) return 0;
    ++writes;
    if (inplace) ++targetWrites;
    CHECK(inplace == !strcmp(name, path));
    if (!f->exists) { f->exists = 1; f->size = 0; f->cluster = 17; }
    if (!inplace) f->size = 0;
    checkpoint(); /* open; r+b does not unlink or free the target chain */
    if (prefixSize) {
        writePart(f, 0, prefix, prefixSize / 2);
        if (shortWrite == 1) return 0;
        writePart(f, 0, prefix, prefixSize);
    }
    writePart(f, prefixSize, data, size / 2);
    if (shortWrite == (inplace ? 3 : 2)) return 0;
    writePart(f, prefixSize, data, size);
    checkpoint(); /* flush */
    if (inplace) { f->size = size; checkpoint(); } /* truncate */
    checkpoint(); /* fsync */
    checkpoint(); /* close */
    return 1;
}
static int removeFile(void* ctx, const char* name) {
    Image* f = find(name);
    (void)ctx;
    if (ioError) return 0;
    CHECK(strcmp(name, path)); /* unlink frees its chain on libfat */
    checkpoint();
    f->exists = 0; f->cluster = 0; f->size = 0;
    checkpoint();
    return 1;
}
static raQueueFileOps ops = {0, readFile, writeFile, removeFile};
static char old[RA_QUEUE_BYTES], next[RA_QUEUE_BYTES], scratch[RA_QUEUE_BYTES];
static void reset(void) {
    memset(files, 0, sizeof(files));
    step = crash = ioError = shortWrite = targetError = writes = targetWrites = 0;
    CHECK(raQueueRecover(path, scratch, sizeof(scratch), &ops));
    files[0].exists = 1; files[0].size = sizeof(old); files[0].cluster = 42;
    memcpy(files[0].data, old, sizeof(old));
    step = 0;
}
static void complete(void) {
    CHECK(files[0].exists && files[0].size == sizeof(old));
    CHECK(files[0].cluster == 42);
    CHECK(!memcmp(files[0].data, old, sizeof(old)) || !memcmp(files[0].data, next, sizeof(next)));
    CHECK(!files[1].exists && !files[2].exists);
}
static Image interrupted[3];
static void recoverCuts(void) {
    memcpy(interrupted, files, sizeof(files));
    step = 0; crash = 0;
    CHECK(raQueueRecover(path, scratch, sizeof(scratch), &ops));
    int recoverySteps = step;
    complete();
    CHECK(raQueueRecover(path, scratch, sizeof(scratch), &ops));
    complete();
    for (int cut = 1; cut <= recoverySteps; ++cut) {
        memcpy(files, interrupted, sizeof(files));
        step = 0; crash = cut;
        if (!setjmp(reboot)) raQueueRecover(path, scratch, sizeof(scratch), &ops);
        crash = 0;
        CHECK(raQueueRecover(path, scratch, sizeof(scratch), &ops));
        complete();
        CHECK(raQueueRecover(path, scratch, sizeof(scratch), &ops));
        complete();
    }
}
int main(void) {
    const char* reply = "HTTP/1.1 200 OK\r\nContent-Length: 16\r\n\r\n{\"Success\":true}";
    CHECK(raQueueReplyComplete(reply, strlen(reply), 0));
    CHECK(!raQueueReplyComplete(reply, strlen(reply) - 1, 1));
    CHECK(!raQueueReplyComplete("HTTP/1.1 503 Error\r\n\r\n{\"Success\":true}", strlen("HTTP/1.1 503 Error\r\n\r\n{\"Success\":true}"), 1));
    CHECK(!raQueueReplyComplete("HTTP/1.1 200 OK\r\n\r\n{\"Success\":true}", strlen("HTTP/1.1 200 OK\r\n\r\n{\"Success\":true}"), 0));
    CHECK(raQueueReplyComplete("HTTP/1.1 200 OK\r\n\r\n{\"Success\":true}", strlen("HTTP/1.1 200 OK\r\n\r\n{\"Success\":true}"), 1));
    CHECK(raQueueAwardResult(1, 0, "User already has this achievement awarded.") == RA_AWARD_ACCEPTED);
    CHECK(raQueueAwardResult(1, 0, "User already has hardcore and regular achievements awarded.") == RA_AWARD_ACCEPTED);
    char chunked[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nC\r\n{\"Success\":t\r\n4\r\nrue}\r\n0\r\n\r\n";
    CHECK(raQueueReplyDecode(chunked, strlen(chunked), 0));
    CHECK(!strcmp(chunked, "{\"Success\":true}"));
    CHECK(raNetJsonTrue(chunked, "Success"));
    const char* refusals[] = {"Bad signature", "Please retry", "Unknown error", "", "Invalid token", "Achievement not already unlocked; retry"};
    CHECK(raQueueAwardResult(1, 1, 0) == RA_AWARD_ACCEPTED);
    CHECK(raQueueAwardResult(1, 0, "User already unlocked this achievement") == RA_AWARD_ACCEPTED);
    CHECK(raQueueAwardResult(1, 0, "Unknown achievement") == RA_AWARD_REFUSED);
    CHECK(raQueueAwardResult(1, 0, "Achievement not found") == RA_AWARD_REFUSED);
    for (unsigned i = 0; i < sizeof(refusals) / sizeof(*refusals); ++i)
        CHECK(raQueueAwardResult(1, 0, refusals[i]) == RA_AWARD_PENDING);
    CHECK(raQueueAwardResult(0, 0, "Unknown achievement") == RA_AWARD_PENDING);
    CHECK(raQueueAwardResult(0, 1, 0) == RA_AWARD_PENDING);
    CHECK(raQueueAwardResult(1, 0, "Already unlocked") == RA_AWARD_ACCEPTED);
    puts("submit: accepted, already unlocked, definitive/unknown refusal, HTTP error and timeout classifications passed");
    /* The fake server accepted but its response was lost; retry is an already-unlocked acknowledgment. */
    int owed = 1;
    if (raQueueAwardResult(0, 1, 0) == RA_AWARD_ACCEPTED) owed = 0;
    CHECK(owed == 1);
    if (raQueueAwardResult(1, 0, "Already unlocked") == RA_AWARD_ACCEPTED) owed = 0;
    CHECK(owed == 0);
    raQueue q, scanned;
    int keep[2] = {0, 1};
    raQueueScan(&q, "100 101", 7);
    q.terminal[0] = 1;
    CHECK(raQueuePack(&q, keep, 2, old, sizeof(old)) == 2);
    raQueueScan(&scanned, old, sizeof(old));
    CHECK(scanned.count == 2 && scanned.terminal[0] && !scanned.terminal[1]);
    CHECK(raQueuePack(&q, keep + 1, 1, next, sizeof(next)) == 1);
    puts("submit: lost acceptance response retries harmlessly; refused marker survives 48-byte round trip");
    memset(old, 0, sizeof(old));
    memset(next, 0, sizeof(next));
    for (int i = 0; i < RA_QUEUE_MAX; ++i) {
        snprintf(old + i * RA_QUEUE_RECORD, RA_QUEUE_RECORD, "%d\n", 100000 + i);
        snprintf(next + i * RA_QUEUE_RECORD, RA_QUEUE_RECORD, "%d\n", 200000 + i);
    }
    raQueueScan(&scanned, old, sizeof(old));
    CHECK(scanned.count == RA_QUEUE_MAX);
    raQueueScan(&scanned, next, sizeof(next));
    CHECK(scanned.count == RA_QUEUE_MAX);
    reset();
    CHECK(raQueueReplace(path, next, sizeof(next), &ops));
    int steps = step;
    CHECK(steps == 17);
    complete();
    for (int cut = 1; cut <= steps; ++cut) {
        reset(); crash = cut;
        if (!setjmp(reboot)) raQueueReplace(path, next, sizeof(next), &ops);
        crash = 0;
        recoverCuts();
    }
    for (int partial = 1; partial <= 3; ++partial) {
        reset(); shortWrite = partial;
        CHECK(!raQueueReplace(path, next, sizeof(next), &ops));
        CHECK(!raQueueAvailable(path));
        if (partial == 3) {
            CHECK(memcmp(files[0].data, old, sizeof(old)) != 0);
            CHECK(memcmp(files[0].data, next, sizeof(next)) != 0);
        } else CHECK(!memcmp(files[0].data, old, sizeof(old)));
        shortWrite = 0;
        recoverCuts();
        CHECK(raQueueAvailable(path));
    }
    reset(); targetError = 1;
    CHECK(!raQueueReplace(path, next, sizeof(next), &ops));
    CHECK(files[2].exists && !memcmp(files[0].data, old, sizeof(old)));
    CHECK(!raQueueRecover(path, scratch, sizeof(scratch), &ops));
    CHECK(!raQueueAvailable(path));
    targetError = 0;
    recoverCuts();
    CHECK(!memcmp(files[0].data, next, sizeof(next)));
    for (int field = 0; field < 6; ++field) {
        reset(); targetError = 1;
        CHECK(!raQueueReplace(path, next, sizeof(next), &ops));
        targetError = 0;
        if (field < 4) files[2].data[field * 4] ^= 1;
        if (field == 4) files[2].data[16 + RA_QUEUE_BYTES / 2] ^= 1;
        if (field == 5) files[2].size++;
        recoverCuts();
        CHECK(!memcmp(files[0].data, old, sizeof(old)));
        CHECK(targetWrites == 0);
    }
    reset();
    files[1] = files[0]; files[1].cluster = 99;
    memcpy(files[1].data, next, sizeof(next));
    recoverCuts();
    CHECK(!memcmp(files[0].data, old, sizeof(old)));
    reset(); ioError = 1;
    CHECK(!raQueueReplace(path, next, sizeof(next), &ops));
    CHECK(!raQueueRecover(path, scratch, sizeof(scratch), &ops));
    ioError = 0;
    CHECK(raQueueRecover(path, scratch, sizeof(scratch), &ops));
    complete();
    printf("journal: crashes at all %d write/flush/truncate/fsync/close/remove steps and every recovery step yield complete old or new queue\n", steps);
    puts("journal: partial header/payload/target, invalid magic/version/length/checksum, oversized redo, stale .new and repeated recovery passed; target cluster preserved");
    puts("journal: failed replay disables queue until recovery succeeds; no rename or target unlink");
    printf("durability: PASSED (%u checks)\n", checks);
    return 0;
}
