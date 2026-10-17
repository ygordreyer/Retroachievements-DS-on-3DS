#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "nds/ndstypes.h"
#define TWOCARD
#define fatCard2 1
#define fatCached 2
#define CLUSTER_FREE 0
#define CLUSTER_EOF 0xFFFFFFFFu
#define BYTES_PER_SECTOR 512
#define tonccpy memcpy
static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } } while (0)
typedef struct aFile {
    u32 firstCluster, currentCluster, currentOffset, fatTableSettings;
    u32* fatTableCache;
} aFile;
static u32 discBytePerClus[2] = {2048, 2048}, discSecPerClus[2] = {4, 4};
static u32 prevFirstClust[2], prevClust[2];
static int prevSect[2] = {-1, -1};
static char globalBuffer[2][512], disk[8192];
static int readFails, writeFails, reads, writes;
static u32 findCluster(aFile* f, u32 at) { f->currentCluster = f->firstCluster + at / 2048; return at / 2048; }
static u32 getCachedCluster(aFile* f, u32 index) { return f->firstCluster + index; }
static u32 FAT_NextCluster(u32 c, bool card) { (void)card; return c + 1; }
static u32 FAT_ClustToSect(u32 c, bool card) { (void)card; return (c - 1) * 4; }
static bool CARD_ReadSectors(u32 at, int n, void* out, bool card) {
    (void)card;
    ++reads;
    CHECK(at * 512 + n * 512 <= sizeof(disk));
    if (readFails) return false;
    memcpy(out, disk + at * 512, n * 512);
    return true;
}
static bool CARD_WriteSectors(u32 at, int n, const void* in, bool card) {
    (void)card;
    ++writes;
    CHECK(at * 512 + n * 512 <= sizeof(disk));
    if (writeFails == writes) return false;
    memcpy(disk + at * 512, in, n * 512);
    return true;
}
#define CARD_WriteSector(at, in, card) CARD_WriteSectors(at, 1, in, card)
#include "ra_fat_functions.inc"
int main(void) {
    char input[1200];
    memset(input, 0x5A, sizeof(input));
    for (int card = 0; card < 2; ++card) {
        aFile f = {1, 1, 0, card, 0};
        prevSect[card] = -1;
        reads = writes = 0; readFails = 1; writeFails = 0;
        CHECK(fileWrite(input, &f, 0, 48) == 0 && writes == 0);
        readFails = 0; writeFails = 1;
        CHECK(fileWrite(input, &f, 0, 48) == 0 && writes == 1);
        writes = 0; writeFails = 0;
        CHECK(fileWrite(input, &f, 0, 48) == 48);
        CHECK(!memcmp(input, disk, 48));
        writes = 0; writeFails = 2;
        CHECK(fileWrite(input, &f, 480, 48) == 0);
        writes = 0; writeFails = 0;
        CHECK(fileWrite(input, &f, 480, 48) == 48);
        CHECK(!memcmp(input, disk + 480, 48));
        writes = 0; writeFails = 2;
        CHECK(fileWrite(input, &f, 0, 1200) == 0);
        writes = 0; writeFails = 3;
        CHECK(fileWrite(input, &f, 0, 1200) == 0);
        writes = 0; writeFails = 0;
        CHECK(fileWrite(input, &f, 0, 1200) == 1200);
        CHECK(!memcmp(input, disk, 1200));
    }
    printf("FAT: read-modify-write read failure and first/middle/last sector write failure propagate, both cards (%u checks)\n", checks);
    return 0;
}
