#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ra.h"
#include "ra_wifi.h"

#include "ra_rc_frame.h"
#include "locations.h"

static vu32 testChannel[CARDENGINE_SHARED_SLOTS];
#define RA_UNLOCK_CHANNEL(address) testChannel
static raRcDiagnostics published;
static raUnlockDiagnostics unlockPublished;
static raSessionBlock testSession;
static raViewerBlock testViewer;
#undef CARDENGINEI_ARM9_RA_UNLOCK_LOCATION
#define CARDENGINEI_ARM9_RA_UNLOCK_LOCATION (&unlockPublished)
#undef CARDENGINEI_ARM9_RA_SESSION_LOCATION
#define CARDENGINEI_ARM9_RA_SESSION_LOCATION (&testSession)
#undef CARDENGINEI_ARM9_RA_VIEWER_LOCATION
#define CARDENGINEI_ARM9_RA_VIEWER_LOCATION (&testViewer)
#undef CARDENGINEI_ARM9_RA_DIAGNOSTICS_LOCATION
#define CARDENGINEI_ARM9_RA_DIAGNOSTICS_LOCATION (&published)
#include "../retail/cardenginei/arm9_ra/source/ra_rcheevos.c"

static unsigned checks;
#define CHECK(c) do { \
    ++checks; \
    if (!(c)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); \
        exit(1); \
    } \
} while (0)

static uint8_t memory[128];
static unsigned reads;

bool ra_readable(u32 address, u32 length) {
    return length <= sizeof(memory) && address >= RA_DS_SYSTEM_RAM_BASE
        && address - RA_DS_SYSTEM_RAM_BASE <= sizeof(memory) - length;
}

u32 ra_read(u32 address, u8 size) {
    u32 value = 0;
    unsigned i;
    CHECK(ra_readable(address, size));
    ++reads;
    for (i = 0; i < size; ++i)
        value |= (u32)memory[address - RA_DS_SYSTEM_RAM_BASE + i] << (i * 8);
    return value;
}

int ra_watch_add(u32 base, u8 size, u8 depth, const u32* offsets) {
    (void)base; (void)size; (void)depth; (void)offsets;
    CHECK(0);
    return -1;
}
int ra_watch_add_flags(u32 base, u8 size, u8 depth, const u32* offsets, u8 flags) {
    (void)flags;
    return ra_watch_add(base, size, depth, offsets);
}
void ra_watch_clear(void) { CHECK(0); }
const void* ra_text_render(const char* a, const char* b) {
    (void)a; (void)b;
    CHECK(0);
    return NULL;
}

static uint16_t clock_line(void* unused) {
    (void)unused;
    return 192;
}

static void run(void) {
    CHECK(ra_rc_frame_run(&frameState, &runtime, ra_rc_event_handler, ra_rc_peek,
        NULL, clock_line, NULL));
    CHECK(frameState.current_trigger_id == 0);
    CHECK(frameState.diagnostics.trigger_evaluations
        == frameState.diagnostics.frames_evaluated * frameState.diagnostics.active_triggers);
}

int main(void) {
    raSnapshot snapshot = {0};
    unsigned i;
    const u32 id = RA_SYNTHETIC_ID_BASE + 100;

    memset(&frameState, 0, sizeof(frameState));
    memset(&published, 0xA5, sizeof(published));
    rcStage = RA_RC_PARSE_BAD;
    for (i = 1; i <= 3; ++i) {
        ra_rc_tick(&snapshot);
        CHECK(published.frames_not_evaluated == i);
        CHECK(published.frames_evaluated == 0 && published.trigger_evaluations == 0);
    }
    CHECK(published.magic == RA_DIAGNOSTICS_MAGIC);
    CHECK(published.version == RA_DIAGNOSTICS_VERSION);
    CHECK(published.size == sizeof(published));
    puts("diagnostics: pre-activation hooks count skipped frames and publish RAD1/version/size");

    rc_runtime_init(&runtime);
    CHECK(runtime.memrefs != NULL);
    CHECK(rc_runtime_activate_achievement(&runtime, id, "0xH1000000=1", 0, 0) == RC_OK);
    CHECK(rc_runtime_activate_achievement(&runtime, id + 1, "0xH1003FFF=1", 0, 0) == RC_OK);
    CHECK(!ra_rc_validate_address(0x1000000));
    CHECK(!ra_rc_validate_address(0x1003FFF));
    CHECK(ra_rc_validate_address(0x3FFFFF));
    CHECK(!ra_rc_validate_address(0x400000));
    rc_runtime_validate_addresses(&runtime, ra_rc_event_handler, ra_rc_validate_address);
    ra_rc_publish_diagnostics();
    CHECK(published.disabled_count == 2);
    CHECK(published.first_disabled_id == id + 1);
    CHECK(published.first_disabled_address == 0x1003FFF);
    CHECK(published.rejected_reads == 0 && peeksRejected == 0);
    CHECK(rc_runtime_get_achievement(&runtime, id)->state == RC_TRIGGER_STATE_DISABLED);
    for (i = 0; i < 4; ++i)
        run();
    CHECK(frameState.diagnostics.rejected_reads == 0 && reads == 0);
    CHECK(frameState.diagnostics.frames_not_evaluated == 3);
    CHECK(frameState.diagnostics.active_triggers == 2);
    rc_runtime_destroy(&runtime);
    puts("diagnostics: DTCM endpoints disable synthetic IDs with first ID/address and zero rejected peeks");

    memset(&frameState, 0, sizeof(frameState));
    rc_runtime_init(&runtime);
    CHECK(runtime.memrefs != NULL);
    CHECK(rc_runtime_activate_achievement(&runtime, id + 2,
        "I:0xX000000_0xH000004=1", 0, 0) == RC_OK);
    rc_runtime_validate_addresses(&runtime, ra_rc_event_handler, ra_rc_validate_address);
    CHECK(frameState.diagnostics.disabled_count == 0);
    memory[3] = 1;
    for (i = 1; i <= 4; ++i) {
        run();
        ra_rc_publish_diagnostics();
        CHECK(published.rejected_reads == i);
        CHECK(published.first_rejected_address == 0x1000004);
        CHECK(published.first_rejected_trigger == 0);
        CHECK(published.frames_not_evaluated == 0);
        CHECK(published.frames_evaluated == i && published.active_triggers == 1);
    }
    CHECK(peeksRejected == 4);
    memory[3] = 2;
    run();
    CHECK(frameState.diagnostics.first_rejected_address == 0x1000004);
    CHECK(frameState.diagnostics.first_rejected_trigger == 0);
    frameState.unsupported = 1;
    for (i = 1; i <= 4; ++i) {
        CHECK(!ra_rc_frame_run(&frameState, &runtime, ra_rc_event_handler, ra_rc_peek,
            NULL, clock_line, NULL));
        ra_rc_publish_diagnostics();
        CHECK(published.frames_not_evaluated == i);
        CHECK(published.frames_evaluated == 5 && published.trigger_evaluations == 5);
        CHECK(published.rejected_reads == 5);
    }
    rc_runtime_destroy(&runtime);
    puts("diagnostics: indirect rejection address/trigger latch, visits = frames x triggers, stopped skips passed");
    {
        vu32* channel = testChannel;
        memset((void*)channel, 0, sizeof(testChannel));
        snapshot.shared = CARDENGINE_SHARED_ADDRESS_SDK1;
        testSession.magic = RA_SESSION_MAGIC;
        testSession.queue = RA_QUEUE_LEVEL_FULL;
        unlockQueued = unlockOffered = unlockSent = unlockLost = 0;
        unlockHead = unlockTail = 0;
        for (u32 id = 1; id <= 128; ++id) ra_rc_queue_unlock(id);
        CHECK(unlockQueued == 128 && unlockLost == 0);
        ra_rc_offer_unlock(&snapshot);
        CHECK(channel[RA_SHARED_UNLOCK_ID] == 1 && unlockOffered == 1);
        CHECK(unlockSent == 0 && unlockPublished.pending == 128);
        channel[RA_SHARED_UNLOCK_REQ] = 0;
        channel[RA_SHARED_UNLOCK_PROGRESS] = 1;
        ra_rc_offer_unlock(&snapshot);
        CHECK(unlockPublished.accepted == 1 && unlockSent == 0);
        CHECK(unlockPublished.pending == 128 && unlockOffered == 2);
        channel[RA_SHARED_UNLOCK_PROGRESS] = 1u << 16;
        ra_rc_offer_unlock(&snapshot);
        CHECK(unlockSent == 1 && unlockPublished.pending == 127);
        while (unlockQueued) {
            channel[RA_SHARED_UNLOCK_REQ] = 0;
            ra_rc_offer_unlock(&snapshot);
        }
        channel[RA_SHARED_UNLOCK_REQ] = 0;
        ra_rc_offer_unlock(&snapshot);
        CHECK(unlockPublished.accepted == 128 && unlockPublished.committed == 1);
        CHECK(unlockPublished.pending == 127 && unlockLost == 0);
        channel[RA_SHARED_UNLOCK_PROGRESS] = 128u << 16;
        ra_rc_offer_unlock(&snapshot);
        CHECK(unlockSent == 128 && unlockPublished.pending == 0);
        CHECK(unlockPublished.magic == RA_UNLOCK_DIAGNOSTICS_MAGIC);
        CHECK(unlockPublished.version == 1 && unlockPublished.size == sizeof(unlockPublished));
        puts("diagnostics: 128 ARM9 offers stay unsafe after RAM acknowledgment until SD commit sequence advances");
    }
    printf("diagnostics: PASSED (%u checks)\n", checks);
    return 0;
}
