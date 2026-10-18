#ifndef RA_BADGE_H
#define RA_BADGE_H
#include <stdint.h>

#define RA_BADGE_DESCRIPTOR_MAGIC 0x31424152u
#define RA_BADGE_SESSION_OFFSET 0x80
#define RA_BADGE_COMMAND 0x47444142u
#define RA_BADGE_BYTES 544
#define RA_BADGE_TILE_OFFSET 0x2000
#define RA_BADGE_TILE_BYTES 512
#define RA_BADGE_PALETTE_BANK 6
#define RA_BADGE_MAP_OFFSET 0x7800
#define RA_BADGE_MAP_BYTES 1536
#define RA_BADGE_PALETTE_BYTES 512
#define RA_BADGE_BACKUP_BYTES (RA_BADGE_TILE_OFFSET + RA_BADGE_TILE_BYTES)

typedef struct raBadgeDescriptor {
    uint32_t magic, game, cluster, count;
} raBadgeDescriptor;

typedef enum raBadgeStatus {
    RA_BADGE_OK, RA_BADGE_NO_FILE, RA_BADGE_INVALID, RA_BADGE_IO,
    RA_BADGE_CRC, RA_BADGE_MISSING, RA_BADGE_PLACEHOLDER
} raBadgeStatus;

typedef uint32_t (*raBadgeRead)(void*, void*, uint32_t, uint32_t);
typedef struct raBadgeIndex {
    uint32_t count, data;
    unsigned checked;
    raBadgeStatus status;
} raBadgeIndex;

static inline uint32_t raBadgeCrc(const void* data, unsigned bytes) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = ~0u;
    while (bytes--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static inline uint16_t raBadgeGray(uint16_t color) {
    unsigned y = (77 * (color & 31) + 150 * ((color >> 5) & 31)
                  + 29 * ((color >> 10) & 31) + 128) >> 8;
    return (uint16_t)(y | (y << 5) | (y << 10));
}

static inline uint16_t raBadgeMap(unsigned x, unsigned y) {
    return (uint16_t)(RA_BADGE_TILE_OFFSET / 32 + y * 4 + x
                      + (RA_BADGE_PALETTE_BANK << 12));
}

static inline void raBadgePlaceholder(uint16_t* image) {
    for (unsigned i = 0; i < RA_BADGE_BYTES / 2; ++i) image[i] = 0;
    image[1] = 0x4210;
    for (unsigned y = 0; y < 32; ++y) {
        for (unsigned x = 0; x < 32; ++x) {
            if (x == 0 || x == 31 || y == 0 || y == 31 || x == y || x + y == 31) {
                unsigned pixel = ((y / 8) * 4 + x / 8) * 64 + (y % 8) * 8 + x % 8;
                image[16 + pixel / 4] |= 1u << ((pixel % 4) * 4);
            }
        }
    }
}

/* The caller supplies little-endian aligned words, including on the host. */
static inline raBadgeStatus raBadgeValidate(raBadgeIndex* index, raBadgeRead read,
        void* file, uint32_t game, uint32_t bytes) {
    uint32_t h[16], e[8], previous = 0;
    if (!game || bytes < 64) return RA_BADGE_NO_FILE;
    if (read(file, h, 0, sizeof(h)) != sizeof(h)) return RA_BADGE_IO;
    if (h[0] != 0x43424152u || h[1] != 0x00400001u || h[2] != game
        || h[3] > 4096 || h[4] != 0x00200020u || h[5] != 1
        || h[6] != 64 || h[7] != 32 || h[8] != 64 + h[3] * 32
        || h[9] != h[8] + h[3] * RA_BADGE_BYTES || h[9] != bytes
        || h[12] || h[13] || h[14] || h[15]) return RA_BADGE_INVALID;
    for (uint32_t i = 0; i < h[3]; ++i) {
        if (read(file, e, 64 + i * 32, sizeof(e)) != sizeof(e)) return RA_BADGE_IO;
        if (e[0] <= previous || e[1] != h[8] + i * RA_BADGE_BYTES
            || e[2] != RA_BADGE_BYTES || e[4] > 1 || e[5] || e[6] || e[7])
            return RA_BADGE_INVALID;
        previous = e[0];
    }
    index->count = h[3];
    index->data = h[8];
    return RA_BADGE_OK;
}

/* The launcher validated the directory. ARM7 only searches that prepared file. */
static inline raBadgeStatus raBadgeFetch(raBadgeRead read, void* file,
        uint32_t count, uint32_t id, uint16_t* image, volatile uint32_t* crc) {
    uint32_t e[8], lo = 0, hi = count;
    uint32_t data = 64 + hi * 32;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (read(file, e, 64 + mid * 32, sizeof(e)) != sizeof(e)) return RA_BADGE_IO;
        if (e[0] < id) lo = mid + 1;
        else if (e[0] > id) hi = mid;
        else {
            if (e[1] != data + mid * RA_BADGE_BYTES || e[2] != RA_BADGE_BYTES) return RA_BADGE_INVALID;
            if (read(file, image, e[1], RA_BADGE_BYTES) != RA_BADGE_BYTES) return RA_BADGE_IO;
            *crc = e[3];
            return e[4] ? RA_BADGE_PLACEHOLDER : RA_BADGE_OK;
        }
    }
    return RA_BADGE_MISSING;
}

static inline raBadgeStatus raBadgeCheckImage(const uint16_t* image, uint32_t crc) {
    if (raBadgeCrc(image, RA_BADGE_BYTES) != crc) return RA_BADGE_CRC;
    if (image[0]) return RA_BADGE_INVALID;
    for (unsigned i = 1; i < 16; ++i)
        if (image[i] & 0x8000) return RA_BADGE_INVALID;
    return RA_BADGE_OK;
}

/* Host composition of the launch, ARM7 and IGM checks. No whole-pack payload read. */
static inline raBadgeStatus raBadgeLoad(raBadgeIndex* index, raBadgeRead read,
        void* file, uint32_t game, uint32_t bytes, uint32_t id, uint16_t* image) {
    uint32_t crc;
    raBadgeStatus status;
    if (!index->checked) {
        index->status = raBadgeValidate(index, read, file, game, bytes);
        index->checked = 1;
    }
    if (index->status != RA_BADGE_OK) return index->status;
    status = raBadgeFetch(read, file, index->count, id, image, &crc);
    if (status == RA_BADGE_OK || status == RA_BADGE_PLACEHOLDER) {
        raBadgeStatus check = raBadgeCheckImage(image, crc);
        if (check != RA_BADGE_OK) return check;
    }
    return status;
}
#endif
