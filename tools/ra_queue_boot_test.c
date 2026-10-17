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
int main(void) {
    char directory[] = "/tmp/ra-queue-boot.XXXXXX";
    struct stat before, after;
    CHECK(mkdtemp(directory) != 0);
    CHECK(chdir(directory) == 0);
    CHECK(mkdir("sd:", 0700) == 0);
    CHECK(mkdir("fat:", 0700) == 0);
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
