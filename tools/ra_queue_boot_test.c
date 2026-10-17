#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ra_wifi.h"
#include "ra_queue_io.h"

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static char payload[RA_QUEUE_BYTES], scratch[RA_QUEUE_BYTES];
static void journal(const char* path) {
    uint32_t header[4] = {0x314A4152u, 1, RA_QUEUE_BYTES, 2166136261u};
    for (unsigned i = 0; i < sizeof(payload); ++i)
        header[3] = (header[3] ^ (unsigned char)payload[i]) * 16777619u;
    FILE* f = fopen(path, "wb");
    CHECK(f != 0);
    CHECK(fwrite(header, 1, sizeof(header), f) == sizeof(header));
    CHECK(fwrite(payload, 1, sizeof(payload), f) == sizeof(payload));
    CHECK(fclose(f) == 0);
}
static void expect(const char* path) {
    struct stat st;
    FILE* f = fopen(path, "rb");
    CHECK(f != 0);
    CHECK(fread(scratch, 1, sizeof(scratch), f) == sizeof(scratch));
    CHECK(fgetc(f) == EOF && !ferror(f));
    CHECK(fclose(f) == 0);
    CHECK(!memcmp(scratch, payload, sizeof(payload)));
    CHECK(stat(path, &st) == 0 && st.st_size == RA_QUEUE_BYTES);
}
static void put(const void* data, size_t size) {
    FILE* f = fopen(RA_QUEUE_PATH, "wb");
    CHECK(f != 0 && fwrite(data, 1, size, f) == size && fclose(f) == 0);
}
static int refuseWrite(void* context, const char* path, const void* prefix, size_t prefixSize,
                       const void* data, size_t size, int inplace) {
    (void)context; (void)path; (void)prefix; (void)prefixSize;
    (void)data; (void)size; (void)inplace;
    return 0;
}

static void prepare_cases(void) {
    unsigned char input[RA_QUEUE_BYTES + RA_QUEUE_RECORD];
    struct stat before, after;
    CHECK(raQueuePrepare(RA_QUEUE_PATH, &raQueueStdFileOps));
    expect(RA_QUEUE_PATH);
    put(input, 0);
    CHECK(raQueuePrepare(RA_QUEUE_PATH, &raQueueStdFileOps));
    expect(RA_QUEUE_PATH);
    for (unsigned width = 16; width <= 48; width += 16) {
        size_t size = width == 48 ? 2 * width : width * RA_QUEUE_MAX;
        memset(input, 0, sizeof(input));
        memset(payload, 0, sizeof(payload));
        memcpy(input, "100\n", 4);
        memcpy(input + width, width == 16 ? "101\n" : "101\t20261016120000\n",
               width == 16 ? 4 : 19);
        memcpy(payload, input, 4);
        memcpy(payload + RA_QUEUE_RECORD, input + width, width == 16 ? 4 : 19);
        if (width == 48) input[47] = payload[47] = '!';
        put(input, size);
        CHECK(stat(RA_QUEUE_PATH, &before) == 0);
        CHECK(raQueuePrepare(RA_QUEUE_PATH, &raQueueStdFileOps));
        expect(RA_QUEUE_PATH);
        CHECK(stat(RA_QUEUE_PATH, &after) == 0 && before.st_ino == after.st_ino);
        CHECK(access(RA_QUEUE_PATH ".txn", F_OK) != 0);
    }
    memset(payload, 0, sizeof(payload));
    memcpy(payload, "100\n", 4);
    memcpy(payload + RA_QUEUE_RECORD, "101\n", 4);
    put("100\n101\n", 8);
    CHECK(raQueuePrepare(RA_QUEUE_PATH, &raQueueStdFileOps));
    expect(RA_QUEUE_PATH);
    for (unsigned bad = 0; bad < 8; ++bad) {
        size_t size;
        memset(input, 0, sizeof(input));
        if (bad == 0) { memcpy(input, "unknown", 7); size = 7; }
        else if (bad == 1) { memcpy(input, "100\t202610", 10); size = 10; }
        else if (bad == 2) { memcpy(input, "4294967296\n", 11); size = 11; }
        else if (bad == 3) {
            size = sizeof(input);
            for (unsigned i = 0; i <= RA_QUEUE_MAX; ++i)
                memcpy(input + i * RA_QUEUE_RECORD, "100\n", 4);
        } else {
            size = 32 * RA_QUEUE_MAX;
            memcpy(input, "100\t20261016120000", 18);
            memcpy(input + 32, "101\n", 4);
            if (bad == 5) memcpy(input, "100\n101\n", 8);
            if (bad == 6) { memset(input, 0, 32); memcpy(input + 1, "100\n", 4); }
            if (bad == 7) { input[18] = '\n'; input[31] = 'X'; }
        }
        put(input, size);
        CHECK(stat(RA_QUEUE_PATH, &before) == 0);
        CHECK(!raQueuePrepare(RA_QUEUE_PATH, &raQueueStdFileOps));
        CHECK(!raQueueAvailable(RA_QUEUE_PATH));
        FILE* f = fopen(RA_QUEUE_PATH, "rb");
        unsigned char actual[sizeof(input)];
        CHECK(f && fread(actual, 1, sizeof(actual), f) == size && fclose(f) == 0);
        CHECK(!memcmp(input, actual, size));
        CHECK(stat(RA_QUEUE_PATH, &after) == 0 && before.st_ino == after.st_ino && after.st_size == (off_t)size);
        CHECK(access(RA_QUEUE_PATH ".txn", F_OK) != 0);
        CHECK(raQueueRecover(RA_QUEUE_PATH, scratch, sizeof(scratch), &raQueueStdFileOps));
    }
    {
        raQueueFileOps failing = raQueueStdFileOps;
        failing.write = refuseWrite;
        put("100\n", 4);
        CHECK(!raQueuePrepare(RA_QUEUE_PATH, &failing));
        CHECK(!raQueueAvailable(RA_QUEUE_PATH));
        FILE* f = fopen(RA_QUEUE_PATH, "rb");
        CHECK(f && fread(input, 1, sizeof(input), f) == 4 && fclose(f) == 0);
        CHECK(!memcmp(input, "100\n", 4));
        CHECK(access(RA_QUEUE_PATH ".txn", F_OK) != 0);
        CHECK(raQueueRecover(RA_QUEUE_PATH, scratch, sizeof(scratch), &raQueueStdFileOps));
    }
    CHECK(remove(RA_QUEUE_PATH) == 0);
    memset(payload, 0, sizeof(payload));
    puts("queue prepare: missing/empty, legacy 16/32/48-byte records and text IDs migrate in place; unknown/truncated/overflow/over-capacity files untouched and disabled");
    puts("queue prepare: unterminated legacy metadata, extra records, misalignment and invalid padding refused without changing file bytes");
}

int main(void) {
    char directory[] = "/tmp/ra-queue-boot.XXXXXX";
    struct stat before, after;
    CHECK(mkdtemp(directory) != 0);
    CHECK(chdir(directory) == 0);
    CHECK(mkdir("sd:", 0700) == 0);
    CHECK(mkdir("fat:", 0700) == 0);
    prepare_cases();
    memcpy(payload, "100\n", 4);
    FILE* f = fopen(RA_QUEUE_PATH, "wb");
    CHECK(f != 0 && fputs("partial", f) >= 0 && fclose(f) == 0);
    CHECK(stat(RA_QUEUE_PATH, &before) == 0);
    journal(RA_QUEUE_PATH ".txn");
    f = fopen(RA_QUEUE_PATH ".new", "wb");
    CHECK(f != 0 && fputs("never install me", f) >= 0 && fclose(f) == 0);
    CHECK(raQueueBootRecover(RA_QUEUE_PATH));
    expect(RA_QUEUE_PATH);
    CHECK(stat(RA_QUEUE_PATH, &after) == 0 && after.st_ino == before.st_ino);
    CHECK(access(RA_QUEUE_PATH ".txn", F_OK) != 0);
    CHECK(access(RA_QUEUE_PATH ".new", F_OK) != 0);
    /* Recovery is once per mount, so a later session cannot replay old data. */
    f = fopen(RA_QUEUE_PATH, "ab");
    CHECK(f != 0 && fputs("trailing bytes", f) >= 0 && fclose(f) == 0);
    CHECK(stat(RA_QUEUE_PATH, &after) == 0 && after.st_size > RA_QUEUE_BYTES);
    payload[0] = '2';
    CHECK(raQueueReplace(RA_QUEUE_PATH, payload, sizeof(payload), &raQueueStdFileOps));
    CHECK(raQueueBootRecover(RA_QUEUE_PATH));
    expect(RA_QUEUE_PATH);
    CHECK(stat(RA_QUEUE_PATH, &after) == 0 && after.st_ino == before.st_ino);
    CHECK(mkdir(RA_QUEUE_PATH_FAT, 0700) == 0);
    journal(RA_QUEUE_PATH_FAT ".txn");
    CHECK(!raQueueBootRecover(RA_QUEUE_PATH_FAT));
    CHECK(!raQueueAvailable(RA_QUEUE_PATH_FAT));
    CHECK(access(RA_QUEUE_PATH_FAT ".txn", F_OK) == 0);
    CHECK(!raQueueReplace(RA_QUEUE_PATH_FAT, payload, sizeof(payload), &raQueueStdFileOps));
    CHECK(rmdir(RA_QUEUE_PATH_FAT) == 0);
    CHECK(!raQueueBootRecover(RA_QUEUE_PATH_FAT));
    CHECK(access(RA_QUEUE_PATH_FAT, F_OK) != 0);
    CHECK(raQueueRecover(RA_QUEUE_PATH_FAT, scratch, sizeof(scratch), &raQueueStdFileOps));
    CHECK(raQueueAvailable(RA_QUEUE_PATH_FAT));
    expect(RA_QUEUE_PATH_FAT);
    CHECK(remove(RA_QUEUE_PATH) == 0);
    CHECK(remove(RA_QUEUE_PATH_FAT) == 0);
    CHECK(rmdir("sd:") == 0 && rmdir("fat:") == 0);
    CHECK(chdir("/") == 0 && rmdir(directory) == 0);
    printf("boot recovery: WIFI=%d, replay before use, inode preserved, missing target created, failed replay blocks appends, once per boot passed (%u checks)\n", RA_LAUNCHER_WIFI, checks);
    return 0;
}
