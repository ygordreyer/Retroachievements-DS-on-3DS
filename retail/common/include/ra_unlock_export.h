#ifndef RA_UNLOCK_EXPORT_H
#define RA_UNLOCK_EXPORT_H

#include "locations.h"
#include "ra.h"

#define RA_UNLOCK_EXPORT_LINES 263u
#define RA_UNLOCK_EXPORT_IDLE_FRAMES 3u
#define RA_UNLOCK_EXPORT_MAX_FRAMES 30u

/* IME is off; the clock must poll REG_VCOUNT. Unaccepted IDs remain owned by the ring/mailbox. */
static inline void raUnlockExport(volatile uint32_t* shared, uint32_t channel,
                                  unsigned (*offer)(uint32_t), uint16_t (*clock)(void)) {
    unsigned total = 0, idle = 0, previousRemaining = ~0u;
    uint32_t previousReq = shared[RA_SHARED_UNLOCK_REQ];
    uint32_t previousId = shared[RA_SHARED_UNLOCK_ID];
    uint32_t previousProgress = shared[RA_SHARED_UNLOCK_PROGRESS];
    uint16_t previousLine = clock();

    while (total < RA_UNLOCK_EXPORT_MAX_FRAMES * RA_UNLOCK_EXPORT_LINES
        && idle < RA_UNLOCK_EXPORT_IDLE_FRAMES * RA_UNLOCK_EXPORT_LINES) {
        unsigned remaining, elapsed;
        uint16_t line;
        uint32_t req, id, progress;
        if ((shared[RA_SHARED_UNLOCK_PROGRESS] & 255u) >= RA_UNLOCK_PENDING_MAX) break;
        remaining = offer(channel);
        if (!remaining) break;
        line = clock();
        elapsed = (line + RA_UNLOCK_EXPORT_LINES - previousLine) % RA_UNLOCK_EXPORT_LINES;
        previousLine = line;
        total += elapsed;
        req = shared[RA_SHARED_UNLOCK_REQ];
        id = shared[RA_SHARED_UNLOCK_ID];
        progress = shared[RA_SHARED_UNLOCK_PROGRESS];
        idle = remaining == previousRemaining && req == previousReq
            && id == previousId && progress == previousProgress ? idle + elapsed : 0;
        previousRemaining = remaining;
        previousReq = req;
        previousId = id;
        previousProgress = progress;
    }
}

#endif
