#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "ra.h"
#include "ra_wifi.h"
#include "locations.h"
#include "ra_progress_ui.h"
#include "ra_rc_frame.h"

static raViewerBlock testViewer;
#undef CARDENGINEI_ARM9_RA_VIEWER_LOCATION
#define CARDENGINEI_ARM9_RA_VIEWER_LOCATION (&testViewer)
static raSessionBlock testSession;
#undef CARDENGINEI_ARM9_RA_SESSION_LOCATION
#define CARDENGINEI_ARM9_RA_SESSION_LOCATION (&testSession)
static raUnlockDiagnostics testUnlock;
#undef CARDENGINEI_ARM9_RA_UNLOCK_LOCATION
#define CARDENGINEI_ARM9_RA_UNLOCK_LOCATION (&testUnlock)
static raRcDiagnostics testDiagnostics;
#undef CARDENGINEI_ARM9_RA_DIAGNOSTICS_LOCATION
#define CARDENGINEI_ARM9_RA_DIAGNOSTICS_LOCATION (&testDiagnostics)
static vu32 testChannel[CARDENGINE_SHARED_SLOTS];
#define RA_UNLOCK_CHANNEL(address) testChannel
#include "../retail/cardenginei/arm9_ra/source/ra_rcheevos.c"

static unsigned checks, reads;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
bool ra_readable(u32 a, u32 n) { (void)a; (void)n; ++reads; return false; }
u32 ra_read(u32 a, u8 n) { (void)a; (void)n; ++reads; return 0; }
int ra_watch_add(u32 a, u8 b, u8 c, const u32* d) { (void)a; (void)b; (void)c; (void)d; CHECK(0); return -1; }
int ra_watch_add_flags(u32 a, u8 b, u8 c, const u32* d, u8 e) { (void)e; return ra_watch_add(a,b,c,d); }
void ra_watch_clear(void) { CHECK(0); }
const void* ra_text_render(const char* a, const char* b) { (void)a; (void)b; CHECK(0); return NULL; }

static void query_tests(void) {
    uint32_t header[9] = {0};
    raProgress p;
    rc_trigger_t* t;
    CHECK(sizeof(p) == 16);
    CHECK(!raProgressSupported(header));
    header[3] = 0x314F4152;
    CHECK(!raProgressSupported(header)); /* old offer-only binary */
    header[5] = RA_PROGRESS_MAGIC;
    header[6] = RA_PROGRESS_VERSION;
    header[7] = RA_PROGRESS_CAP_QUERY;
    header[8] = sizeof(p);
    CHECK(raProgressSupported(header));
    ++header[6]; CHECK(!raProgressSupported(header)); --header[6];
    header[7] = 0; CHECK(!raProgressSupported(header)); header[7] = RA_PROGRESS_CAP_QUERY;
    ++header[8]; CHECK(!raProgressSupported(header)); --header[8];
    memset(&runtime, 0xA5, sizeof(runtime));
    ra_rc_query_progress(1, &p);
    CHECK(p.id == 1 && p.flags == RA_PROGRESS_INITIALIZING && !p.value && !p.target);
    CHECK(!reads); /* no dereference of uninitialized BSS */
    rc_runtime_init(&runtime);
    queryGuard = 0;
    rcStage = RA_RC_ACTIVE;
    ra_rc_query_progress(0, &p); CHECK(p.flags == RA_PROGRESS_MISSING);
    ra_rc_query_progress(999, &p); CHECK(p.id == 999 && p.flags == RA_PROGRESS_MISSING);
    CHECK(rc_runtime_activate_achievement(&runtime, 1, "0xH0000=1", 0, 0) == RC_OK);
    CHECK(rc_runtime_activate_achievement(&runtime, 2, "M:0xH0001=100", 0, 0) == RC_OK);
    ra_rc_query_progress(1, &p); CHECK(p.flags == RA_PROGRESS_UNMEASURED);
    t = rc_runtime_get_achievement(&runtime, 2);
    ra_rc_query_progress(2, &p); CHECK(p.flags == RA_PROGRESS_INITIALIZING);
    t->state = RC_TRIGGER_STATE_ACTIVE;
    t->measured_value = 45;
    ra_rc_query_progress(2, &p);
    CHECK(p.id == 2 && p.value == 45 && p.target == 100 && p.flags == RA_PROGRESS_MEASURED);
    t->measured_as_percent = 1;
    ra_rc_query_progress(2, &p); CHECK(p.flags == (RA_PROGRESS_MEASURED | RA_PROGRESS_PERCENT));
    t->measured_value = 0;
    ra_rc_query_progress(2, &p); CHECK(p.flags & RA_PROGRESS_MEASURED); CHECK(!p.value && p.target == 100);
    t->measured_value = UINT32_MAX - 1;
    t->measured_target = UINT32_MAX;
    ra_rc_query_progress(2, &p); CHECK(raProgressPercent(p.value, p.target) == 99);
    t->state = RC_TRIGGER_STATE_DISABLED;
    ra_rc_query_progress(2, &p); CHECK(p.flags == RA_PROGRESS_DISABLED && !p.value && !p.target);
    t->state = RC_TRIGGER_STATE_INACTIVE;
    ra_rc_query_progress(2, &p); CHECK(p.flags == RA_PROGRESS_DISABLED);
    t->state = RC_TRIGGER_STATE_TRIGGERED;
    t->measured_value = 99; t->measured_target = 100;
    ra_rc_query_progress(2, &p);
    CHECK(p.flags == (RA_PROGRESS_MEASURED | RA_PROGRESS_PERCENT | RA_PROGRESS_EARNED));
    CHECK(p.value == 99); /* preserve the reading, never invent target completion */
    testViewer.magic = RA_VIEWER_MAGIC; testViewer.count = 1;
    testViewer.entry[0].id = 3; testViewer.entry[0].flags = RA_VIEWER_EARNED;
    ra_rc_query_progress(3, &p);
    CHECK(p.flags == (RA_PROGRESS_EARNED | RA_PROGRESS_UNMEASURED) && !p.target);
    rcStage = RA_RC_LOADING;
    testViewer.entry[0].flags = 0;
    ra_rc_query_progress(3, &p); CHECK(p.flags == RA_PROGRESS_INITIALIZING);
    ra_rc_query_progress(999, &p); CHECK(p.flags == RA_PROGRESS_MISSING);
    rcStage = RA_RC_ACTIVE;
    testViewer.entry[0].flags = RA_VIEWER_SESSION | RA_VIEWER_UNSAVED;
    ra_rc_query_progress(3, &p); CHECK(p.flags & RA_PROGRESS_EARNED);
    {
        rc_trigger_t before = *t;
        for (unsigned i = 0; i < 128; ++i) ra_rc_query_progress(2, &p);
        CHECK(!memcmp(&before, t, sizeof(before)) && !reads);
    }
    ra_rc_query_progress(2, NULL);
    rc_runtime_destroy(&runtime);
    puts("query ABI: absent/old runtime, version/capability/size, bad ID, initializing, raw, percent, zero, disabled, earned and overflow passed; no game reads or trigger mutation");
}

static void ui_tests(void) {
    char out[33], clipped[24];
    raProgress p = {1, 45, 100, RA_PROGRESS_MEASURED};
    raProgressFormat(out, &p); CHECK(!strcmp(out, "45/100"));
    p.flags |= RA_PROGRESS_PERCENT;
    raProgressFormat(out, &p); CHECK(!strcmp(out, "45%"));
    CHECK(raProgressBarFill(&p, 5) == 2);
    raProgressBar(out, &p, 5); CHECK(!strcmp(out, "[==...]"));
    p.value = 1; p.target = 3; CHECK(raProgressBarFill(&p, 3) == 1);
    p.value = 45; p.target = 100;
    p.value = 0;
    raProgressFormat(out, &p); CHECK(!strcmp(out, "0%"));
    CHECK(!raProgressBarFill(&p, 28));
    p.value = 101; CHECK(raProgressBarFill(&p, 28) == 28);
    p.value = UINT32_MAX; CHECK(raProgressPercent(p.value, p.target) == 100);
    p.value = UINT32_MAX - 1; p.target = UINT32_MAX;
    CHECK(raProgressPercent(p.value, p.target) == 99);
    p.flags = RA_PROGRESS_MEASURED;
    raProgressFormat(out, &p); CHECK(!strcmp(out, "4294967294/4294967295"));
    CHECK(strlen(out) + 11 <= 32); /* list bar plus worst-case raw reading */
    p.target = 0; CHECK(!raProgressPercent(p.value, p.target));
    p.flags = RA_PROGRESS_UNMEASURED;
    raProgressFormat(out, &p); CHECK(!strcmp(out, "No progress data"));
    CHECK(!raProgressBarFill(&p, 28));
    p.flags = RA_PROGRESS_INITIALIZING;
    raProgressFormat(out, &p); CHECK(!strcmp(out, "Initializing"));
    p.flags = RA_PROGRESS_DISABLED;
    raProgressFormat(out, &p); CHECK(!strcmp(out, "Disabled"));
    CHECK(raProgressClip(clipped, "abcdefghijklmnopqrstuvwxyz12345", 23) == 23);
    CHECK(!strcmp(clipped, "abcdefghijklmnopqrstuv~"));
    CHECK(3 + strlen(clipped) < 27); /* points occupy columns 27 to 31 */
    raProgressClip(out, "short", 23); CHECK(!strcmp(out, "short"));
    raProgressClip(out, "abcd", 4); CHECK(!strcmp(out, "abcd"));
    raProgressClip(out, "abcd", 1); CHECK(!strcmp(out, "~"));
    raProgressClip(out, "abcd", 0); CHECK(!*out);
    for (unsigned value = 0; value <= 100; ++value) {
        p.flags = RA_PROGRESS_MEASURED; p.value = value; p.target = 100;
        CHECK(raProgressBarFill(&p, 28) <= 28);
        CHECK(raProgressBarFill(&p, 28) == value * 28 / 100);
    }
    puts("IGM helpers: raw/percent/no-data text, maximum 32-bit values, title/points clipping, empty/full and all percentage bar boundaries passed");
}

static void queue_tests(void) {
    raProgress p;
    rc_runtime_init(&runtime);
    memset(&testViewer, 0, sizeof(testViewer));
    testViewer.magic = RA_VIEWER_MAGIC; testViewer.count = 3;
    testViewer.entry[0].id = 1000;
    testViewer.entry[1].id = 1001; testViewer.entry[1].flags = RA_VIEWER_EARNED;
    testViewer.entry[2].id = 1002; testViewer.entry[2].flags = RA_VIEWER_QUEUED;
    testViewer.earned = testViewer.queued = 1;
    unlockHead = unlockTail = unlockQueued = unlockLost = 0;
    for (unsigned i = 0; i < RA_UNLOCK_RING; ++i) ra_rc_queue_unlock(100 + i);
    CHECK(unlockQueued == RA_UNLOCK_RING);
    ra_rc_queue_unlock(1000);
    CHECK(unlockLost == 1 && unlockQueued == RA_UNLOCK_RING);
    CHECK(testViewer.entry[0].flags == (RA_VIEWER_SESSION | RA_VIEWER_UNSAVED));
    CHECK(testViewer.queued == 1 && testViewer.earned == 1);
    ra_rc_query_progress(1000, &p); CHECK(p.flags & RA_PROGRESS_EARNED);
    ra_rc_queue_unlock(1001); ra_rc_queue_unlock(1002);
    CHECK(!(testViewer.entry[1].flags & RA_VIEWER_QUEUED));
    CHECK(!(testViewer.entry[2].flags & RA_VIEWER_EARNED));
    CHECK(testViewer.queued == 1 && testViewer.earned == 1);
    unlockHead = unlockTail = unlockQueued = 0;
    testSession.magic = RA_SESSION_MAGIC; testSession.queue = RA_QUEUE_LEVEL_OFF;
    ra_rc_queue_unlock(1000);
    CHECK(testViewer.entry[0].flags == (RA_VIEWER_SESSION | RA_VIEWER_QUEUED));
    CHECK(testViewer.queued == 2 && testViewer.earned == 1);
    CHECK(!ra_rc_offer_paused(CARDENGINE_SHARED_ADDRESS_SDK1) && unlockQueued == 1);
    CHECK(!testChannel[RA_SHARED_UNLOCK_REQ]);
    ra_rc_queue_unlock(1001); ra_rc_queue_unlock(1002);
    CHECK(testViewer.queued == 2 && testViewer.earned == 1);
    for (unsigned i = 0; i < testViewer.count; ++i)
        CHECK((testViewer.entry[i].flags & (RA_VIEWER_EARNED | RA_VIEWER_QUEUED)) != (RA_VIEWER_EARNED | RA_VIEWER_QUEUED));
    {
        const unsigned queued = unlockQueued;
        ra_rc_queue_unlock(0); ra_rc_queue_unlock(RA_SYNTHETIC_ID_BASE);
        CHECK(unlockQueued == queued);
    }
    rc_runtime_destroy(&runtime);
    puts("unlock ring: full-ring session earned survives refusal, queued count unchanged, queue-off remains RAM-only, already-earned/queued disjoint, synthetic IDs rejected");
}

int main(void) {
    query_tests(); ui_tests(); queue_tests();
    printf("paused progress: PASSED (%u checks)\n", checks);
    return 0;
}
