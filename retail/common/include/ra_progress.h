#ifndef RA_PROGRESS_H
#define RA_PROGRESS_H

/* Append-only WRAM header contract. Offsets 0, 4, 8 and 12 are unchanged. */
#define RA_PROGRESS_ENTRY_OFFSET 16
#define RA_PROGRESS_MAGIC_OFFSET 20
#define RA_PROGRESS_VERSION_OFFSET 24
#define RA_PROGRESS_CAPS_OFFSET 28
#define RA_PROGRESS_SIZE_OFFSET 32
#define RA_PROGRESS_MAGIC 0x31514152
#define RA_PROGRESS_VERSION 1
#define RA_PROGRESS_CAP_QUERY 1
#define RA_PROGRESS_RESULT_SIZE 16

#ifndef __ASSEMBLER__
#include <stdint.h>

enum {
    RA_PROGRESS_INITIALIZING = 1,
    RA_PROGRESS_MISSING = 2,
    RA_PROGRESS_DISABLED = 4,
    RA_PROGRESS_UNMEASURED = 8,
    RA_PROGRESS_MEASURED = 16,
    RA_PROGRESS_PERCENT = 32,
    RA_PROGRESS_EARNED = 64
};

typedef struct {
    uint32_t id, value, target, flags;
} raProgress;
typedef char raProgressSizeCheck[sizeof(raProgress) == RA_PROGRESS_RESULT_SIZE ? 1 : -1];
typedef void (*raProgressQuery)(uint32_t, raProgress*);

static inline int raProgressSupported(const volatile uint32_t* header) {
    return header[3] == 0x314F4152u
        && header[RA_PROGRESS_MAGIC_OFFSET / 4] == RA_PROGRESS_MAGIC
        && header[RA_PROGRESS_VERSION_OFFSET / 4] == RA_PROGRESS_VERSION
        && (header[RA_PROGRESS_CAPS_OFFSET / 4] & RA_PROGRESS_CAP_QUERY)
        && header[RA_PROGRESS_SIZE_OFFSET / 4] == sizeof(raProgress);
}

static inline unsigned raProgressPercent(uint32_t value, uint32_t target) {
    if (!target) return 0;
    if (value >= target) return 100;
    return (unsigned)(((uint64_t)value * 100u) / target);
}
#endif
#endif
