#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef struct {
    u32 firstCluster, currentCluster, currentOffset, fatTableSettings;
    u32* fatTableCache;
} aFile;

#define BYTES_PER_SECTOR 512
#define CLUSTER_FREE 0
#define CLUSTER_EOF 0x0fffffffu
#define fatCached 1
#define fatCard2 2
#define tonccpy memcpy

static unsigned char disk[8 * BYTES_PER_SECTOR];
static u32 chain[9], clusterCount;
#ifdef TWOCARD
static unsigned char globalBuffer[2][BYTES_PER_SECTOR];
static u32 discBytePerClus[2] = {512, 512};
static u32 discSecPerClus[2] = {1, 1};
static u32 discSecPerClusShift[2];
#else
static unsigned char globalBuffer[BYTES_PER_SECTOR];
static u32 discBytePerClus = 512, discSecPerClus = 1, discSecPerClusShift;
#endif

static u32 getCachedCluster(aFile* file, u32 index) {
    (void)file;
    return index < clusterCount ? chain[index] : CLUSTER_EOF;
}

static u32 findCluster(aFile* file, u32 offset) {
    u32 index = offset / BYTES_PER_SECTOR;
    file->currentCluster = getCachedCluster(file, index);
    file->currentOffset = index * BYTES_PER_SECTOR;
    return index;
}

static u32 FAT_NextCluster(u32 cluster
#ifdef TWOCARD
                           , bool card
#endif
) {
#ifdef TWOCARD
    (void)card;
#endif
    for (u32 i = 0; i < clusterCount; ++i)
        if (chain[i] == cluster) return getCachedCluster(NULL, i + 1);
    assert(false);
    return CLUSTER_EOF;
}

static u32 FAT_ClustToSect(u32 cluster
#ifdef TWOCARD
                           , bool card
#endif
) {
#ifdef TWOCARD
    (void)card;
#endif
    assert(cluster >= 2 && cluster < 10);
    return cluster - 2;
}

static void CARD_ReadSectors(u32 sector, u32 count, char* dst
#ifdef TWOCARD
                             , bool card
#endif
) {
#ifdef TWOCARD
    (void)card;
#endif
    assert(sector + count <= 8);
    memcpy(dst, disk + sector * BYTES_PER_SECTOR, count * BYTES_PER_SECTOR);
}

static bool loadSectorBuf(aFile* file, unsigned sector) {
#ifdef TWOCARD
    bool card = (file->fatTableSettings & fatCard2) != 0;
    CARD_ReadSectors(FAT_ClustToSect(file->currentCluster, card) + sector, 1,
                     (char*)globalBuffer[card], card);
#else
    CARD_ReadSectors(FAT_ClustToSect(file->currentCluster) + sector, 1,
                     (char*)globalBuffer);
#endif
    return true;
}

#include "ra_badge_file_read.h"

static void checkRead(u32 offset, u32 length, u32 expected, u32 settings) {
    unsigned char output[2048];
    assert(length < sizeof(output));
    memset(output, 0xa5, sizeof(output));
    aFile file = {chain[0], 0, 0, settings, chain};
    assert(fileRead((char*)output, &file, offset, length) == expected);
    for (u32 i = 0; i < expected; ++i)
        assert(output[i] == (unsigned char)((offset + i) * 17 + (offset + i) / 256));
    for (u32 i = expected; i < sizeof(output); ++i) assert(output[i] == 0xa5);
}

int main(void) {
    for (unsigned fragmented = 0; fragmented < 2; ++fragmented) {
        for (u32 i = 0; i < 8; ++i) {
            chain[i] = 2 + (fragmented ? (i * 3) % 8 : i);
            for (u32 j = 0; j < BYTES_PER_SECTOR; ++j) {
                u32 offset = i * BYTES_PER_SECTOR + j;
                disk[(chain[i] - 2) * BYTES_PER_SECTOR + j] = offset * 17 + offset / 256;
            }
        }
        chain[8] = CLUSTER_EOF;
        for (u32 settings = 0; settings < 4; ++settings) {
            clusterCount = 8;
            const u32 offsets[] = {160, 704, 1248};
            for (unsigned i = 0; i < 3; ++i) checkRead(offsets[i], 544, 544, settings);
            checkRead(0, 1, 1, settings);
            checkRead(48, 1, 1, settings);
            checkRead(0, 512, 512, settings);
            checkRead(160, 1500, 1500, settings);
            for (unsigned i = 0; i < 3; ++i) {
                clusterCount = offsets[i] / BYTES_PER_SECTOR + 1;
                checkRead(offsets[i], 544, BYTES_PER_SECTOR - offsets[i] % BYTES_PER_SECTOR, settings);
            }
            clusterCount = 2;
            checkRead(160, 1500, 864, settings);
            checkRead(1024, 544, 0, settings);
        }
    }
    puts("badge FAT: real fileRead, offsets 160/704/1248 with 544 bytes, full sectors, fragmented/cached chains and short EOF counts passed");
    return 0;
}
