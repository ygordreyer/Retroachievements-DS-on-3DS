#ifndef RA_RC_FRAME_H
#define RA_RC_FRAME_H

#include <stdint.h>
#include "rc_runtime.h"

#define RA_RC_OVERRUN_LIMIT 30
#define RA_RC_SCANLINES_PER_FRAME 263
#define RA_RC_VBLANK_FIRST_LINE 192
#define RA_RC_CLOCK_STRIDE 8

typedef struct raRcFrameState {
    uint16_t cost;
    uint16_t room;
    uint16_t cost_max;
    uint16_t other_transitions;
    uint8_t room_max;
    uint8_t late_start_frames;
    uint8_t overruns;
    uint8_t unsupported;
} raRcFrameState;

typedef uint16_t (*raRcFrameClock)(void* userdata);

/* Zero-initialize once per session. Only TRIGGERED transitions call the handler;
 * other_transitions counts the remaining evaluation state transitions in the last frame.
 * Unsupported is latched until the session ends.
 * Room ends at line 263 for blanking starts, or at line 192 for late starts.
 * The clock returns VCOUNT in [0, 262]; each memref pass or trigger stride must take
 * less than one frame. Elapsed cost accumulates across wraps, saturated at 0xFFFF. */
int ra_rc_frame_run(raRcFrameState* state, rc_runtime_t* runtime,
                    rc_runtime_event_handler_t handler, rc_runtime_peek_t peek,
                    void* peek_userdata, raRcFrameClock clock, void* clock_userdata);

#endif
