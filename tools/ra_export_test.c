#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ra_unlock_export.h"

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static uint32_t shared[CARDENGINE_SHARED_SLOTS], ring[8];
static unsigned queued, accepted, calls, ticks, churn;
static uint16_t line;

static uint16_t clockLine(void) {
    ++ticks;
    line = (line + 1) % RA_UNLOCK_EXPORT_LINES;
    return line;
}
static unsigned stalledOffer(uint32_t channel) {
    CHECK(channel == CARDENGINE_SHARED_ADDRESS_SDK1);
    ++calls;
    return queued;
}
static unsigned acceptingOffer(uint32_t channel) {
    stalledOffer(channel);
    CHECK(queued > 0);
    CHECK(ring[accepted] == 100 + accepted);
    ++accepted;
    return --queued;
}
static unsigned changingOffer(uint32_t channel) {
    stalledOffer(channel);
    if (++churn % (2 * RA_UNLOCK_EXPORT_LINES) == 0)
        shared[RA_SHARED_UNLOCK_ID] ^= 1;
    return queued;
}
static void reset(void) {
    memset(shared, 0, sizeof(shared));
    for (unsigned i = 0; i < 8; ++i) ring[i] = 100 + i;
    queued = 8;
    accepted = calls = ticks = churn = 0;
    line = 260;
}
int main(void) {
    reset();
    shared[RA_SHARED_UNLOCK_REQ] = RA_SHARED_UNLOCK_MAGIC;
    shared[RA_SHARED_UNLOCK_ID] = 99;
    raUnlockExport(shared, CARDENGINE_SHARED_ADDRESS_SDK1, stalledOffer, clockLine);
    CHECK(calls <= (RA_UNLOCK_EXPORT_IDLE_FRAMES + 1) * RA_UNLOCK_EXPORT_LINES);
    CHECK(calls >= RA_UNLOCK_EXPORT_IDLE_FRAMES * RA_UNLOCK_EXPORT_LINES);
    CHECK(queued == 8 && accepted == 0);
    for (unsigned i = 0; i < 8; ++i) CHECK(ring[i] == 100 + i);
    CHECK(shared[RA_SHARED_UNLOCK_REQ] == RA_SHARED_UNLOCK_MAGIC);
    CHECK(shared[RA_SHARED_UNLOCK_ID] == 99);
    puts("export: stalled offer stops after 3 idle frames with ring and mailbox intact");

    reset();
    raUnlockExport(shared, CARDENGINE_SHARED_ADDRESS_SDK1, acceptingOffer, clockLine);
    CHECK(queued == 0 && accepted == 8 && calls == 8);
    puts("export: accepting offer drains all 8 IDs in order");

    reset();
    raUnlockExport(shared, CARDENGINE_SHARED_ADDRESS_SDK1, changingOffer, clockLine);
    CHECK(calls == RA_UNLOCK_EXPORT_MAX_FRAMES * RA_UNLOCK_EXPORT_LINES);
    CHECK(queued == 8 && accepted == 0);
    puts("export: changing mailbox cannot extend the 30-frame total cap");

    reset();
    shared[RA_SHARED_UNLOCK_PROGRESS] = RA_UNLOCK_PENDING_MAX;
    raUnlockExport(shared, CARDENGINE_SHARED_ADDRESS_SDK1, stalledOffer, clockLine);
    CHECK(calls == 0 && queued == 8);
    printf("export: PASSED (%u checks)\n", checks);
    return 0;
}
