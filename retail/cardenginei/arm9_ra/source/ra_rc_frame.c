#include "ra_rc_frame.h"
#include "rcheevos/rc_internal.h"

void ra_rc_record_disabled(raRcFrameState* state, uint32_t id, uint32_t address) {
    if (state->diagnostics.disabled_count == 0) {
        state->diagnostics.first_disabled_id = id;
        state->diagnostics.first_disabled_address = address;
    }
    ++state->diagnostics.disabled_count;
}

static void ra_rc_sample_clock(raRcFrameState* state, raRcFrameClock clock,
                               void* userdata, uint16_t* previous) {
    uint16_t now = clock(userdata);
    unsigned cost = state->cost
        + (now + RA_RC_SCANLINES_PER_FRAME - *previous) % RA_RC_SCANLINES_PER_FRAME;
    *previous = now;
    state->cost = (uint16_t)(cost > 0xFFFF ? 0xFFFF : cost);
}

static void ra_rc_do_frame(raRcFrameState* state, rc_runtime_t* runtime,
                           rc_runtime_event_handler_t handler, rc_runtime_peek_t peek,
                           void* userdata, raRcFrameClock clock, void* clock_userdata,
                           uint16_t* previous) {
    rc_runtime_event_t event;
    int32_t i;
    unsigned clock_triggers = 0;

    state->other_transitions = 0;
    state->current_trigger_id = 0;
    state->diagnostics.active_triggers = 0;
    event.value = 0;
    rc_update_memref_values(runtime->memrefs, peek, userdata);
    ra_rc_sample_clock(state, clock, clock_userdata, previous);
    for (i = (int32_t)runtime->trigger_count - 1; i >= 0; --i) {
        rc_trigger_t* trigger = runtime->triggers[i].trigger;
        int old_state, new_state;

        if (!trigger)
            goto checkpoint;

        ++state->diagnostics.active_triggers;
        ++state->diagnostics.trigger_evaluations;
        state->current_trigger_id = runtime->triggers[i].id;
        old_state = trigger->state;
        if (runtime->triggers[i].invalid_memref) {
            /* Match v12.4.0's disable bookkeeping without evaluating invalid memory. */
            ra_rc_record_disabled(state, runtime->triggers[i].id,
                runtime->triggers[i].invalid_memref->address);
            trigger->state = RC_TRIGGER_STATE_DISABLED;
            runtime->triggers[i].invalid_memref = NULL;
            new_state = trigger->state;
        } else {
            new_state = rc_evaluate_trigger(trigger, peek, userdata, 0);
            /* RESET is a notification; the trigger retains its actual state. */
            if (new_state == RC_TRIGGER_STATE_RESET)
                new_state = trigger->state;
        }

        /* Terminal triggers return INACTIVE; retain the old evaluator's counting. */
        if (new_state == old_state)
            goto checkpoint;
        if (new_state == RC_TRIGGER_STATE_TRIGGERED) {
            event.type = RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED;
            event.id = runtime->triggers[i].id;
            handler(&event);
        } else {
            ++state->other_transitions;
        }
checkpoint:
        if (++clock_triggers == RA_RC_CLOCK_STRIDE) {
            ra_rc_sample_clock(state, clock, clock_userdata, previous);
            clock_triggers = 0;
        }
    }
    state->current_trigger_id = 0;
    ra_rc_sample_clock(state, clock, clock_userdata, previous);
}

int ra_rc_frame_run(raRcFrameState* state, rc_runtime_t* runtime,
                    rc_runtime_event_handler_t handler, rc_runtime_peek_t peek,
                    void* peek_userdata, raRcFrameClock clock, void* clock_userdata) {
    uint16_t start, previous;

    if (!runtime || state->unsupported) {
        ++state->diagnostics.frames_not_evaluated;
        return 0;
    }

    start = clock(clock_userdata);
    previous = start;
    state->cost = 0;
    state->room = start >= RA_RC_VBLANK_FIRST_LINE
        ? RA_RC_SCANLINES_PER_FRAME - start : RA_RC_VBLANK_FIRST_LINE - start;
    if (start < RA_RC_VBLANK_FIRST_LINE && state->late_start_frames < 255)
        ++state->late_start_frames;
    ra_rc_do_frame(state, runtime, handler, peek, peek_userdata,
                   clock, clock_userdata, &previous);
    ++state->diagnostics.frames_evaluated;

    if (state->cost > state->cost_max)
        state->cost_max = state->cost;
    if (state->room > state->room_max)
        state->room_max = (uint8_t)state->room;

    if (state->cost > state->room) {
        if (++state->overruns >= RA_RC_OVERRUN_LIMIT)
            state->unsupported = 1;
    } else {
        state->overruns = 0;
    }
    return 1;
}
