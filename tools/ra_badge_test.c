#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ra_badge.h"
#include "ra_progress_ui.h"

typedef uint8_t u8;
typedef unsigned FontPalette;
enum { FONT_LIGHT_BLUE, FONT_LIGHT_GRAY };
static struct { bool rtl; } igmText;
static uint16_t menuMap[32 * 24];
#define BG_MAP_RAM_SUB(n) menuMap

static void print(int x, int y, const unsigned char* text, FontPalette color, bool main) {
    (void)color;
    (void)main;
    assert(x >= 1 && x + strlen((const char*)text) <= 27 && y >= 0 && y < 24);
    while (*text) menuMap[y * 32 + x++] = *text++;
}

#include "ra_badge_layout.h"

static void cursorTests(void) {
    for (unsigned rtl = 0; rtl < 2; ++rtl) {
        igmText.rtl = rtl;
        for (unsigned slot = 0; slot < 10; ++slot) {
            unsigned y = slot == 9 ? 19 : 3 + 2 * slot;
            memset(menuMap, 0, sizeof(menuMap));
            for (unsigned row = 0; row < 4; ++row)
                for (unsigned col = 0; col < 4; ++col)
                    menuMap[(y + row) * 32 + 27 + col] = raBadgeMap(col, row);
            drawCursor(y);
            for (unsigned row = 0; row < 4; ++row)
                for (unsigned col = 0; col < 4; ++col)
                    assert(menuMap[(y + row) * 32 + 27 + col] == raBadgeMap(col, row));
            assert(menuMap[y * 32 + (rtl ? 31 : 0)] == (rtl ? '<' : '>'));
        }
    }
    const raProgress progress[] = {
        {0, 0xffffffffu, 0xffffffffu, RA_PROGRESS_MEASURED},
        {0, 0, 0xffffffffu, RA_PROGRESS_MEASURED},
        {0, 1, 10, RA_PROGRESS_MEASURED},
        {0, 1, 10, RA_PROGRESS_MEASURED | RA_PROGRESS_PERCENT},
        {0, 0, 0, 0}, {0, 0, 0, RA_PROGRESS_INITIALIZING},
        {0, 0, 0, RA_PROGRESS_DISABLED}
    };
    for (unsigned i = 0; i < sizeof(progress) / sizeof(*progress); ++i)
        raDrawProgress(4, &progress[i], 5);
    puts("badge layout: real RTL/LTR cursor preserves all nine list badges and detail badge; progress text stays before column 27 passed");
}

typedef struct TestFile {
    unsigned char* data;
    uint32_t size, limit, calls, payloads;
} TestFile;

static uint32_t readBytes(void* context, void* dst, uint32_t offset, uint32_t bytes) {
    TestFile* file = context;
    ++file->calls;
    assert(bytes == 32 || bytes == 64 || bytes == RA_BADGE_BYTES);
    if (bytes == RA_BADGE_BYTES) ++file->payloads;
    assert(offset <= file->size && bytes <= file->size - offset);
    if (offset >= file->limit) return 0;
    if (bytes > file->limit - offset) bytes = file->limit - offset;
    memcpy(dst, file->data + offset, bytes);
    return bytes;
}

static raBadgeStatus load(TestFile* file, unsigned game, unsigned id, uint16_t* image) {
    raBadgeIndex index = {0};
    file->calls = file->payloads = 0;
    return raBadgeLoad(&index, readBytes, file, game, file->size, id, image);
}

static void parserTests(unsigned char* golden, uint32_t size) {
    unsigned char data[4096];
    uint16_t image[RA_BADGE_BYTES / 2];
    TestFile file = {data, size, size, 0, 0};
    raBadgeIndex index = {0};
    memcpy(data, golden, size);
    assert(raBadgeCrc(data + 64, size - 64) == ((uint32_t*)data)[10]);
    assert(load(&file, 42, 10, image) == RA_BADGE_OK && file.payloads == 1);
    assert(!image[0]);
    for (unsigned i = 16; i < RA_BADGE_BYTES / 2; ++i) assert(!image[i]);
    assert(load(&file, 42, 20, image) == RA_BADGE_OK && file.payloads == 1);
    assert(load(&file, 42, 30, image) == RA_BADGE_PLACEHOLDER && file.payloads == 1);
    assert(load(&file, 42, 99, image) == RA_BADGE_MISSING && !file.payloads);
    assert(load(&file, 43, 20, image) == RA_BADGE_INVALID && !file.payloads);
    assert(load(&file, 0, 20, image) == RA_BADGE_NO_FILE);
    file.size = 0;
    assert(load(&file, 42, 20, image) == RA_BADGE_NO_FILE);
    file.size = size - 1;
    assert(load(&file, 42, 20, image) == RA_BADGE_INVALID);
    file.size = size;
    file.limit = 63;
    assert(load(&file, 42, 20, image) == RA_BADGE_IO);
    file.limit = 64 + 32 + 31;
    assert(load(&file, 42, 20, image) == RA_BADGE_IO);
    file.limit = size - 1;
    assert(load(&file, 42, 30, image) == RA_BADGE_IO);
    file.limit = size;

    /* Mutate every structural header field, excluding the exporter-only CRC/revision. */
    for (unsigned word = 0; word < 16; ++word) {
        if (word == 10 || word == 11) continue;
        memcpy(data, golden, size);
        ((uint32_t*)data)[word] ^= 0x80000000u;
        assert(load(&file, 42, 20, image) == RA_BADGE_INVALID);
    }
    for (unsigned word = 0; word < 8; ++word) {
        memcpy(data, golden, size);
        uint32_t* e = (uint32_t*)(data + 96);
        e[word] = word == 0 ? 10 : word == 4 ? 2 : e[word] ^ 0x80000000u;
        assert(load(&file, 42, 20, image) == (word == 3 ? RA_BADGE_CRC : RA_BADGE_INVALID));
    }
    memcpy(data, golden, size);
    ((uint32_t*)(data + 96))[1] = 0xffffffe0u;
    assert(load(&file, 42, 20, image) == RA_BADGE_INVALID);
    memcpy(data, golden, size);
    ((uint32_t*)(data + 64))[0] = 0;
    assert(load(&file, 42, 20, image) == RA_BADGE_INVALID);
    memcpy(data, golden, size);
    data[160 + RA_BADGE_BYTES + 200] ^= 1;
    assert(load(&file, 42, 20, image) == RA_BADGE_CRC);
    for (unsigned color = 0; color < 2; ++color) {
        memcpy(data, golden, size);
        uint32_t* e = (uint32_t*)(data + 96);
        ((uint16_t*)(data + e[1]))[color] |= color ? 0x8000 : 1;
        e[3] = raBadgeCrc(data + e[1], RA_BADGE_BYTES);
        assert(load(&file, 42, 20, image) == RA_BADGE_INVALID);
    }
    memcpy(data, golden, size);
    ((uint32_t*)data)[11] = 1234;
    assert(load(&file, 42, 20, image) == RA_BADGE_OK);
    assert(raBadgeLoad(&index, readBytes, &file, 42, size, 10, image) == RA_BADGE_OK);
    file.calls = file.payloads = 0;
    assert(raBadgeLoad(&index, readBytes, &file, 42, size, 20, image) == RA_BADGE_OK);
    assert(file.calls <= 3 && file.payloads == 1);
    ((uint32_t*)(data + 96))[1] = 0xffffffe0u;
    file.payloads = 0;
    assert(raBadgeLoad(&index, readBytes, &file, 42, size, 20, image) == RA_BADGE_INVALID);
    assert(!file.payloads);
    memcpy(data, golden, size);
    ((uint32_t*)(data + 96))[2] = 0xffffffffu;
    assert(raBadgeLoad(&index, readBytes, &file, 42, size, 20, image) == RA_BADGE_INVALID);
    memcpy(data, golden, size);
    data[160 + RA_BADGE_BYTES + 200] ^= 1;
    assert(raBadgeLoad(&index, readBytes, &file, 42, size, 20, image) == RA_BADGE_CRC);
    memcpy(data, golden, size);
    ((uint32_t*)data)[3] = 0;
    ((uint32_t*)data)[8] = ((uint32_t*)data)[9] = 64;
    file.size = 64;
    assert(load(&file, 42, 20, image) == RA_BADGE_MISSING && !file.payloads);
    puts("badge parser: golden IDs 10/20/30, missing ID/file, identity, header/directory bounds, sorted IDs, flags, short reads, payload CRC and palette passed");
}

static void maximumDirectory(void) {
    unsigned size = 64 + 4096 * (32 + RA_BADGE_BYTES);
    unsigned char* data = calloc(1, size);
    assert(data);
    uint32_t* h = (uint32_t*)data;
    h[0] = 0x43424152; h[1] = 0x400001; h[2] = 42; h[3] = 4096;
    h[4] = 0x200020; h[5] = 1; h[6] = 64; h[7] = 32;
    h[8] = 64 + 4096 * 32; h[9] = size;
    for (unsigned i = 0; i < 4096; ++i) {
        uint32_t* e = (uint32_t*)(data + 64 + i * 32);
        e[0] = i + 1; e[1] = h[8] + i * RA_BADGE_BYTES; e[2] = RA_BADGE_BYTES;
        e[3] = raBadgeCrc(data + e[1], RA_BADGE_BYTES);
    }
    TestFile file = {data, size, size, 0, 0};
    raBadgeIndex index = {0};
    uint16_t image[RA_BADGE_BYTES / 2];
    assert(raBadgeLoad(&index, readBytes, &file, 42, size, 4096, image) == RA_BADGE_OK);
    assert(file.calls <= 1 + 4096 + 13 + 1 && file.payloads == 1);
    for (unsigned id = 1; id <= 4097; ++id) {
        file.calls = file.payloads = 0;
        assert(raBadgeLoad(&index, readBytes, &file, 42, size, id, image)
               == (id == 4097 ? RA_BADGE_MISSING : RA_BADGE_OK));
        assert(file.calls <= 14 && file.payloads == (id != 4097));
    }
    free(data);
    puts("badge reads: 4096-entry ceiling, 32-byte directory reads, at most 13 binary probes and exactly one 544-byte payload per hit passed");
}

static void renderingTests(void) {
    for (unsigned color = 0; color <= 0x7fff; ++color) {
        unsigned expected = (77 * (color & 31) + 150 * ((color >> 5) & 31)
                             + 29 * ((color >> 10) & 31) + 128) / 256;
        assert(raBadgeGray(color) == expected * 0x421);
    }
    uint16_t image[RA_BADGE_BYTES / 2];
    raBadgePlaceholder(image);
    assert(!image[0] && image[1] == 0x4210);
    for (unsigned y = 0; y < 32; ++y) {
        for (unsigned x = 0; x < 32; ++x) {
            unsigned p = ((y / 8) * 4 + x / 8) * 64 + y % 8 * 8 + x % 8;
            assert(((image[16 + p / 4] >> (p % 4 * 4)) & 15)
                   == (x == 0 || x == 31 || y == 0 || y == 31 || x == y || x + y == 31));
        }
    }
    /* Account byte by byte for the production list/detail map placements and restored regions. */
    unsigned char vram[0x8000], original[0x8000], palette[512], palOriginal[512];
    unsigned char backup[RA_BADGE_BACKUP_BYTES], mapBackup[RA_BADGE_MAP_BYTES], palBackup[512];
    memset(vram, 0xa5, sizeof(vram)); memcpy(original, vram, sizeof(vram));
    memset(palette, 0x5a, sizeof(palette)); memcpy(palOriginal, palette, sizeof(palette));
    memcpy(backup, vram, sizeof(backup));
    memcpy(mapBackup, vram + RA_BADGE_MAP_OFFSET, sizeof(mapBackup));
    memcpy(palBackup, palette, sizeof(palBackup));
    for (unsigned offset = RA_BADGE_TILE_OFFSET; offset < RA_BADGE_TILE_OFFSET + RA_BADGE_TILE_BYTES; ++offset) {
        assert(offset < sizeof(backup)); vram[offset] = 0;
    }
    for (unsigned slot = 0; slot < 10; ++slot) {
        unsigned ox = 27, oy = slot == 9 ? 19 : 3 + 2 * slot;
        for (unsigned y = 0; y < 4; ++y) {
            for (unsigned x = 0; x < 4; ++x) {
                uint16_t entry = raBadgeMap(x, y);
                unsigned tile = (entry & 1023) * 32;
                assert(tile >= RA_BADGE_TILE_OFFSET && tile + 32 <= sizeof(backup));
                assert(entry >> 12 == RA_BADGE_PALETTE_BANK && RA_BADGE_PALETTE_BANK >= 6);
                unsigned offset = RA_BADGE_MAP_OFFSET + ((oy + y) * 32 + ox + x) * 2;
                assert(ox + x < 32 && oy + y < 24);
                assert(offset >= RA_BADGE_MAP_OFFSET && offset + 2 <= RA_BADGE_MAP_OFFSET + sizeof(mapBackup));
                memcpy(vram + offset, &entry, 2);
            }
        }
    }
    for (unsigned i = 0; i < 32; ++i) {
        unsigned offset = RA_BADGE_PALETTE_BANK * 32 + i;
        assert(offset < sizeof(palBackup)); palette[offset] = 0;
    }
    memcpy(vram, backup, sizeof(backup));
    memcpy(vram + RA_BADGE_MAP_OFFSET, mapBackup, sizeof(mapBackup));
    memcpy(palette, palBackup, sizeof(palBackup));
    assert(!memcmp(vram, original, sizeof(vram)) && !memcmp(palette, palOriginal, sizeof(palette)));
    raProgress progress = {0, 0xffffffffu, 0xffffffffu, RA_PROGRESS_MEASURED};
    char text[33];
    raProgressFormat(text, &progress);
    assert(strlen(text) == 21 && 5 + 1 + strlen(text) == 27);
    puts("badge rendering: all 32768 RGB555 grayscale colors, transparent placeholder tiling, nine-row numeric fit, tile/map/palette touched ranges and byte restoration passed");
}

int main(void) {
    FILE* f = fopen("tools/fixtures/badges-v1.rab", "rb");
    unsigned char golden[4096];
    assert(f);
    unsigned size = fread(golden, 1, sizeof(golden), f);
    assert(!ferror(f) && feof(f) && size == 1792);
    assert(!fclose(f));
    parserTests(golden, size);
    maximumDirectory();
    renderingTests();
    cursorTests();
    return 0;
}
