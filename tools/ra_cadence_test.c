#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ra_rc_frame.h"
#include "rc_runtime_types.h"
#include "ra_wifi.h"
#include "locations.h"

static unsigned checks;
#define CHECK(c) do { \
    ++checks; \
    if (!(c)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); \
        exit(1); \
    } \
} while (0)

typedef struct Memory {
    uint8_t value[128];
    unsigned reads[128];
} Memory;

typedef struct Clock {
    uint16_t start;
    unsigned cost;
    unsigned calls;
    unsigned intervals;
} Clock;

static uint16_t scanline(void* userdata) {
    Clock* clock = (Clock*)userdata;
    unsigned sample = clock->calls++ % (clock->intervals + 1);
    CHECK((clock->cost + clock->intervals - 1) / clock->intervals < RA_RC_SCANLINES_PER_FRAME);
    return (clock->start + clock->cost * sample / clock->intervals) % RA_RC_SCANLINES_PER_FRAME;
}

static uint32_t peek(uint32_t address, uint32_t size, void* userdata) {
    Memory* memory = (Memory*)userdata;
    CHECK(address < 128 && size == 1);
    ++memory->reads[address];
    return memory->value[address];
}

typedef struct Events {
    rc_runtime_event_t event[512];
    unsigned count;
} Events;
static Events actual_events, reference_events;

static void record(Events* events, const rc_runtime_event_t* event) {
    if (event->type != RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED)
        return;
    CHECK(events->count < 512);
    events->event[events->count++] = *event;
}
static void actual_event(const rc_runtime_event_t* event) {
    CHECK(event->type == RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED);
    record(&actual_events, event);
}
static void reference_event(const rc_runtime_event_t* event) { record(&reference_events, event); }

static void compare_events(void) {
    unsigned i;
    CHECK(actual_events.count == reference_events.count);
    for (i = 0; i < actual_events.count; ++i) {
        CHECK(actual_events.event[i].id == reference_events.event[i].id);
        CHECK(actual_events.event[i].type == reference_events.event[i].type);
        CHECK(actual_events.event[i].value == reference_events.event[i].value);
    }
}

static void activate(rc_runtime_t* runtime, unsigned count, const char* suffix) {
    unsigned i;
    rc_runtime_init(runtime);
    CHECK(runtime->memrefs != NULL);
    for (i = 0; i < count; ++i) {
        char definition[128];
        snprintf(definition, sizeof(definition), "0xH%06x%s", i, suffix);
        /* Delta's second operand references the same per-trigger address. */
        if (strcmp(suffix, "=1_d0xH000000=0") == 0)
            snprintf(definition, sizeof(definition), "0xH%06x=1_d0xH%06x=0", i, i);
        CHECK(rc_runtime_activate_achievement(runtime, RA_SYNTHETIC_ID_BASE + i, definition, 0, 0) == 0);
    }
    CHECK(runtime->trigger_count == count);
}

static void compare_hits(const rc_condset_t* actual, const rc_condset_t* reference) {
    for (; actual && reference; actual = actual->next, reference = reference->next) {
        const rc_condition_t* a = actual->conditions;
        const rc_condition_t* r = reference->conditions;
        for (; a && r; a = a->next, r = r->next)
            CHECK(a->current_hits == r->current_hits);
        CHECK(!a && !r);
    }
    CHECK(!actual && !reference);
}

static void cadence(unsigned count, unsigned scenario) {
    static const char* const suffix[] = {
        "=1", "=1_d0xH000000=0", "=1.3.", "=1.100000.",
        "=1.3._R:0xH000000=2", "=1.3._P:0xH000000=2", "=1.1._T:0xH000000=3"
    };
    static const char* const names[] = {
        "pulse", "delta", "three hits", "every trigger every frame", "reset", "pause", "primed"
    };
    rc_runtime_t actual, reference;
    raRcFrameState state = {0};
    Memory a = {{0}, {0}}, r = {{0}, {0}};
    Clock clock = {192, 10, 0, 2 + count / RA_RC_CLOCK_STRIDE};
    unsigned frame, i, total_triggered = 0;

    activate(&actual, count, suffix[scenario]);
    activate(&reference, count, suffix[scenario]);
    for (frame = 0; frame < 256; ++frame) {
        uint8_t old_states[128];
        unsigned triggered = 0, transitions = 0;
        if (scenario != 3 && frame % 16 == 0) {
            rc_runtime_reset(&actual);
            rc_runtime_reset(&reference);
        }
        for (i = 0; i < count; ++i) {
            a.value[i] = r.value[i] = scenario == 0 ? frame % 16 == 1
                : scenario == 3 ? frame != 0 : frame % 16 != 0;
            if (scenario >= 4)
                a.value[i] = r.value[i] = frame % 4;
            a.reads[i] = r.reads[i] = 0;
            old_states[i] = actual.triggers[i].trigger->state;
        }
        actual_events.count = reference_events.count = 0;
        CHECK(ra_rc_frame_run(&state, &actual, actual_event, peek, &a, scanline, &clock) == 1);
        rc_runtime_do_frame(&reference, reference_event, peek, &r, 0);
        CHECK(!state.unsupported && !state.overruns);
        CHECK(clock.calls == (clock.intervals + 1) * (frame + 1));
        CHECK(state.cost == clock.cost);
        CHECK(state.diagnostics.frames_evaluated == frame + 1);
        CHECK(state.diagnostics.frames_not_evaluated == 0);
        CHECK(state.diagnostics.active_triggers == count);
        CHECK(state.diagnostics.trigger_evaluations == (frame + 1) * count);
        compare_events();
        for (i = 0; i < count; ++i) {
            const rc_trigger_t* at = rc_runtime_get_achievement(&actual, RA_SYNTHETIC_ID_BASE + i);
            const rc_trigger_t* rt = rc_runtime_get_achievement(&reference, RA_SYNTHETIC_ID_BASE + i);
            CHECK(at && rt);
            CHECK(at->state == rt->state);
            compare_hits(at->requirement, rt->requirement);
            compare_hits(at->alternative, rt->alternative);
            transitions += (old_states[i] == RC_TRIGGER_STATE_TRIGGERED
                || old_states[i] == RC_TRIGGER_STATE_DISABLED
                ? RC_TRIGGER_STATE_INACTIVE : actual.triggers[i].trigger->state) != old_states[i];
            CHECK(a.reads[i] == 1 && r.reads[i] == 1);
            if (scenario == 3)
                CHECK(at->requirement->conditions->current_hits == frame);
        }
        for (i = 0; i < actual_events.count; ++i)
            if (actual_events.event[i].type == RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED)
                ++triggered;
        CHECK(state.other_transitions + triggered == transitions);
        if (scenario < 3)
            CHECK(triggered == ((frame % 16 == (scenario == 2 ? 3 : 1)) ? count : 0));
        else if (scenario == 3)
            CHECK(triggered == 0);
        total_triggered += triggered;
    }
    if (scenario < 4)
        CHECK(total_triggered == (scenario == 3 ? 0 : count * 16));
    rc_runtime_destroy(&actual);
    rc_runtime_destroy(&reference);
    printf("cadence: %u triggers, %s, 256 frames passed\n", count,
           names[scenario]);
}

static void budget(void) {
    rc_runtime_t runtime;
    raRcFrameState state = {0};
    Memory memory = {{0}, {0}};
    Clock clock = {192, 71, 0, 2};
    unsigned i;
    activate(&runtime, 1, "=1.100000.");
    actual_events.count = 0;
    CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
    CHECK(state.cost == 71 && state.room == 71 && state.overruns == 0);
    for (i = 0; i < RA_RC_OVERRUN_LIMIT * 4; ++i) {
        clock.cost = i % 3 == 0 ? 72 : 10;
        CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
        CHECK(!state.unsupported);
        CHECK(state.overruns == (i % 3 == 0 ? 1 : 0));
    }
    /* A run just short of the limit is cleared by one frame that fits. */
    clock.cost = 72;
    for (i = 1; i < RA_RC_OVERRUN_LIMIT; ++i) {
        CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
        CHECK(!state.unsupported && state.overruns == i);
    }
    clock.cost = 71;
    CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
    CHECK(state.overruns == 0);
    clock.cost = 101;
    for (i = 1; i <= RA_RC_OVERRUN_LIMIT; ++i) {
        unsigned reads = memory.reads[0];
        CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
        CHECK(memory.reads[0] == reads + 1);
        CHECK(state.overruns == i);
        CHECK(state.unsupported == (i == RA_RC_OVERRUN_LIMIT));
    }
    CHECK(state.cost == 101 && state.room == 71 && state.cost_max == 101 && state.room_max == 71);
    {
        const unsigned reads = memory.reads[0], calls = clock.calls, events = actual_events.count;
        clock.cost = 0;
        clock.start = 0;
        const uint32_t evaluated = state.diagnostics.frames_evaluated;
        CHECK(state.diagnostics.frames_not_evaluated == 0);
        for (i = 0; i < 100; ++i) {
            CHECK(!ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
            CHECK(state.diagnostics.frames_not_evaluated == i + 1);
            CHECK(state.diagnostics.frames_evaluated == evaluated);
            CHECK(state.diagnostics.trigger_evaluations == evaluated);
        }
        CHECK(memory.reads[0] == reads && clock.calls == calls && actual_events.count == events);
        CHECK(state.unsupported && state.overruns == RA_RC_OVERRUN_LIMIT);
        CHECK(state.cost == 101 && state.room == 71);
    }
    rc_runtime_destroy(&runtime);

    activate(&runtime, 1, "=1.100000.");
    memset(&state, 0, sizeof(state));
    clock.start = 191;
    clock.cost = 0;
    clock.calls = 0;
    CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
    CHECK(state.room == 1 && state.cost == 0 && state.overruns == 0);
    CHECK(state.late_start_frames == 1);
    clock.start = 262;
    clock.cost = 1;
    CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
    CHECK(state.room == 1 && state.cost == 1 && state.overruns == 0);
    CHECK(state.late_start_frames == 1);
    clock.cost = 262;
    CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
    CHECK(state.cost == 262 && state.room == 1 && state.overruns == 1);
    CHECK(state.cost_max == 262 && state.room_max == 1);
    {
        static const uint16_t starts[] = {191, 10, 0};
        unsigned s;
        for (s = 0; s < sizeof(starts) / sizeof(starts[0]); ++s) {
            memset(&state, 0, sizeof(state));
            clock.start = starts[s];
            for (i = 0; i < 1000; ++i) {
                clock.cost = 1 + i % 5;
                CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
                CHECK(state.cost == clock.cost && state.room == 192 - starts[s]);
                CHECK(state.room_max == state.room && !state.unsupported);
                CHECK(state.overruns == (starts[s] == 191 ? i % 5 : 0));
                CHECK(state.late_start_frames == (i < 255 ? i + 1 : 255));
            }
        }
    }
    memset(&state, 0, sizeof(state));
    clock.start = 100;
    clock.cost = 120;
    for (i = 1; i <= RA_RC_OVERRUN_LIMIT; ++i) {
        CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
        CHECK(state.overruns == i && state.unsupported == (i == RA_RC_OVERRUN_LIMIT));
    }
    CHECK(state.room == 92 && state.cost == 120);
    CHECK(state.late_start_frames == RA_RC_OVERRUN_LIMIT);
    clock.start = 192;
    CHECK(!ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
    CHECK(state.late_start_frames == RA_RC_OVERRUN_LIMIT);
    rc_runtime_destroy(&runtime);
    puts("budget: isolated overruns, consecutive limit, latched stop, late starts and VCOUNT wrap passed");
}

static void heavy_budget(void) {
    static const unsigned costs[] = {263, 273, 600};
    rc_runtime_t runtime;
    Memory memory = {{0}, {0}};
    Clock clock = {192, 0, 0, 2 + 24 / RA_RC_CLOCK_STRIDE};
    unsigned c, frame;

    activate(&runtime, 24, "=1.100000.");
    for (c = 0; c < sizeof(costs) / sizeof(costs[0]); ++c) {
        raRcFrameState state = {0};
        clock.cost = costs[c];
        clock.calls = 0;
        for (frame = 1; frame <= RA_RC_OVERRUN_LIMIT; ++frame) {
            CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
            CHECK(state.cost == costs[c] && state.cost_max == costs[c]);
            CHECK(state.room == 71 && state.overruns == frame);
            CHECK(state.unsupported == (frame == RA_RC_OVERRUN_LIMIT));
            CHECK(clock.calls == (clock.intervals + 1) * frame);
        }
        {
            unsigned reads = memory.reads[0], calls = clock.calls;
            CHECK(!ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
            CHECK(memory.reads[0] == reads && clock.calls == calls);
            CHECK(state.cost == costs[c]);
        }
    }
    rc_runtime_destroy(&runtime);
    puts("budget: costs of 263, 273 and 600 scanlines accumulate and latch after 30 overruns");
}

static void saturated_budget(void) {
    rc_runtime_t runtime;
    raRcFrameState state = {0};
    Memory memory = {{0}, {0}};
    Clock clock = {192, 0x10000 + 100, 0, 2 + 2048 / RA_RC_CLOCK_STRIDE};

    rc_runtime_init(&runtime);
    CHECK(runtime.memrefs != NULL);
    runtime.trigger_count = runtime.trigger_capacity = 2048;
    runtime.triggers = calloc(runtime.trigger_count, sizeof(*runtime.triggers));
    CHECK(runtime.triggers != NULL);
    CHECK(ra_rc_frame_run(&state, &runtime, actual_event, peek, &memory, scanline, &clock));
    CHECK(clock.calls == clock.intervals + 1);
    CHECK(state.cost == 0xFFFF && state.cost_max == 0xFFFF);
    CHECK(state.overruns == 1 && !state.unsupported);
    rc_runtime_destroy(&runtime);
    puts("budget: null-trigger checkpoints and 16-bit cost saturation passed");
}

static void disabled_and_null(void) {
    rc_runtime_t actual, reference;
    raRcFrameState state = {0};
    Clock clock = {192, 1, 0, 2};
    Memory a = {{0}, {0}}, r = {{0}, {0}};
    rc_trigger_t* saved_actual;
    rc_trigger_t* saved_reference;
    unsigned frame, i;

    activate(&actual, 3, "=1.3.");
    activate(&reference, 3, "=1.3.");
    CHECK(ra_rc_frame_run(&state, &actual, actual_event, peek, &a, scanline, &clock));
    rc_runtime_do_frame(&reference, reference_event, peek, &r, 0);
    rc_runtime_invalidate_address(&actual, 0);
    rc_runtime_invalidate_address(&reference, 0);
    CHECK(actual.triggers[0].invalid_memref && reference.triggers[0].invalid_memref);
    actual.triggers[1].trigger->state = reference.triggers[1].trigger->state = RC_TRIGGER_STATE_DISABLED;
    saved_actual = actual.triggers[2].trigger;
    saved_reference = reference.triggers[2].trigger;
    actual.triggers[2].trigger = reference.triggers[2].trigger = NULL;
    for (frame = 0; frame < 4; ++frame) {
        for (i = 0; i < 3; ++i) {
            a.value[i] = r.value[i] = 1;
            a.reads[i] = r.reads[i] = 0;
        }
        actual_events.count = reference_events.count = 0;
        CHECK(ra_rc_frame_run(&state, &actual, actual_event, peek, &a, scanline, &clock));
        rc_runtime_do_frame(&reference, reference_event, peek, &r, 0);
        compare_events();
        CHECK(actual_events.count == 0);
        CHECK(state.other_transitions == 2);
        CHECK(!actual.triggers[0].invalid_memref && !reference.triggers[0].invalid_memref);
        CHECK(actual.triggers[0].trigger->state == RC_TRIGGER_STATE_DISABLED);
        CHECK(state.diagnostics.disabled_count == 1);
        CHECK(state.diagnostics.first_disabled_id == RA_SYNTHETIC_ID_BASE);
        CHECK(state.diagnostics.first_disabled_address == 0);
        CHECK(a.reads[0] == 0 && r.reads[0] == 0);
        for (i = 0; i < 2; ++i) {
            CHECK(actual.triggers[i].trigger->state == reference.triggers[i].trigger->state);
            compare_hits(actual.triggers[i].trigger->requirement, reference.triggers[i].trigger->requirement);
            CHECK(actual.triggers[i].trigger->requirement->conditions->current_hits == 0);
        }
        CHECK(saved_actual->state == saved_reference->state);
        CHECK(saved_actual->requirement->conditions->current_hits == 0);
    }
    actual.triggers[2].trigger = saved_actual;
    reference.triggers[2].trigger = saved_reference;
    rc_runtime_destroy(&actual);
    rc_runtime_destroy(&reference);
    puts("runtime: invalid memrefs, disabled and null triggers passed");
}

int main(void) {
    static const unsigned sizes[] = {1, 50, 128};
    unsigned i, scenario;
    CHECK(sizeof(raSnapshot) == 0x12C);
    CHECK(offsetof(raSnapshot, linesMax) == 0x1B);
    CHECK(offsetof(raSnapshot, rcRoom) == 0x25);
    CHECK(offsetof(raSnapshot, raMpuBits) == 0x26);
    CHECK(offsetof(raSnapshot, rcCostHigh) == 0x27);
    CHECK(offsetof(raSnapshot, rcOverruns) == 0x69);
    CHECK(offsetof(raSnapshot, rcStackUsed) == 0x6A);
    CHECK(offsetof(raSnapshot, rcStage) == 0x6C);
    CHECK(offsetof(raSnapshot, rcLines) == 0x84);
    CHECK(offsetof(raSnapshot, rcRoomMax) == 0x87);
    CHECK(offsetof(raSnapshot, rcLateStartFrames) == 0xC3);
    CHECK(offsetof(raSnapshot, defsMagic) == 0xC4);
    CHECK(offsetof(raSnapshot, notifySeq) == 0xE0);
    CHECK(offsetof(raSnapshot, notifyTitle) == 0xEC);
    CHECK(offsetof(raSessionBlock, rcStage) == 8);
    CHECK(sizeof(raSessionBlock) <= CARDENGINEI_ARM9_RA_SESSION_MAX);
    CHECK(sizeof(raRcDiagnostics) == 0x30);
    CHECK(offsetof(raRcDiagnostics, magic) == 0x00);
    CHECK(offsetof(raRcDiagnostics, version) == 0x04);
    CHECK(offsetof(raRcDiagnostics, size) == 0x06);
    CHECK(offsetof(raRcDiagnostics, disabled_count) == 0x08);
    CHECK(offsetof(raRcDiagnostics, first_disabled_id) == 0x0C);
    CHECK(offsetof(raRcDiagnostics, first_disabled_address) == 0x10);
    CHECK(offsetof(raRcDiagnostics, rejected_reads) == 0x14);
    CHECK(offsetof(raRcDiagnostics, first_rejected_address) == 0x18);
    CHECK(offsetof(raRcDiagnostics, first_rejected_trigger) == 0x1C);
    CHECK(offsetof(raRcDiagnostics, frames_evaluated) == 0x20);
    CHECK(offsetof(raRcDiagnostics, frames_not_evaluated) == 0x24);
    CHECK(offsetof(raRcDiagnostics, trigger_evaluations) == 0x28);
    CHECK(offsetof(raRcDiagnostics, active_triggers) == 0x2C);
    CHECK(CARDENGINEI_ARM9_RA_DIAGNOSTICS_LOCATION == 0x0377F310);
    CHECK(CARDENGINEI_ARM9_RA_DIAGNOSTICS_LOCATION >= CARDENGINEI_ARM9_RA_SESSION_LOCATION + sizeof(raSessionBlock));
    CHECK(CARDENGINEI_ARM9_RA_DIAGNOSTICS_LOCATION + sizeof(raRcDiagnostics)
        <= CARDENGINEI_ARM9_RA_SESSION_LOCATION + CARDENGINEI_ARM9_RA_SESSION_MAX);
    CHECK(sizeof(raRcDiagnostics) <= CARDENGINEI_ARM9_RA_DIAGNOSTICS_MAX);
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
        for (scenario = 0; scenario < 7; ++scenario)
            cadence(sizes[i], scenario);
    budget();
    heavy_budget();
    saturated_budget();
    disabled_and_null();
    printf("cadence: PASSED (%u checks)\n", checks);
    return 0;
}
