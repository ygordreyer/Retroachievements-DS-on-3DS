#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "ra.h"
#include "ra_wifi.h"
#include "locations.h"

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define RA_UNLOCK_STATE_MAGIC 0x314C5541u
static u32 channel[CARDENGINE_SHARED_SLOTS];
static vu32* sharedAddr = channel;
static u32 raUnlocksCluster;
static int raUnlocksFile;
static int prevSect[2];
static char disk[RA_QUEUE_BYTES];
static char raStampCache[] = "20261016120000";
static char raGameCode[] = "TEST", raGameTitle[] = "DURABILITY  ";
static int readFail, writeFail, writes, transientWrites, transientReads, criticalEnters, failOnWrite;
static int readOngoing;
static int ime = 1;
static int enterCriticalSection(void) { int old = ime; ime = 0; ++criticalEnters; return old; }
static void leaveCriticalSection(int old) { CHECK(ime == 0); ime = old; }
#ifndef TWLSDK
static unsigned vcount, resumeCalls, readCompletesAfter;
#define REG_VCOUNT vcount
static bool resume_cardRead_arm9(void) {
    CHECK(ime == 0 && readOngoing && writes == 0);
    ++resumeCalls;
    vcount = (vcount + 1) % 263;
    if (readCompletesAfter && resumeCalls == readCompletesAfter) readOngoing = 0;
    return !readOngoing;
}
#endif
#define toncset memset
#define tonccpy memmove
static u32 fileRead(char* out, int* file, u32 at, u32 n) {
    (void)file;
    if (transientReads) { --transientReads; return 0; }
    if (readFail || at + n > sizeof(disk)) return 0;
    memcpy(out, disk + at, n);
    return n;
}
static u32 fileWrite(const char* in, int* file, u32 at, u32 n) {
    (void)file;
    CHECK(!readOngoing);
    ++writes;
    CHECK(at + n <= sizeof(disk));
    if (writeFail || transientWrites || writes == failOnWrite) {
        if (transientWrites) --transientWrites;
        memcpy(disk + at, in, n / 2);
        return n / 2;
    }
    memcpy(disk + at, in, n);
    return n;
}
#include "../retail/cardenginei/arm7/source/ra_unlock.inc"

static void reset(void) {
    memset(channel, 0, sizeof(channel));
    memset(disk, 0, sizeof(disk));
    readOngoing = 0;
    ime = 1;
#ifndef TWLSDK
    vcount = 260;
    resumeCalls = readCompletesAfter = 0;
#endif
    raUnlockStateMagic = 0;
    raUnlocksCluster = 1;
    readFail = writeFail = writes = transientWrites = transientReads = criticalEnters = failOnWrite = 0;
}
static void offer(u32 id) {
    sharedAddr[RA_SHARED_UNLOCK_ID] = id;
    sharedAddr[RA_SHARED_UNLOCK_LEVEL] = RA_QUEUE_LEVEL_FULL;
    sharedAddr[RA_SHARED_UNLOCK_REQ] = RA_SHARED_UNLOCK_MAGIC;
    raUnlockService();
}
static unsigned pending(void) { return channel[RA_SHARED_UNLOCK_PROGRESS] & 255u; }
static unsigned committed(void) { return channel[RA_SHARED_UNLOCK_PROGRESS] >> 16; }
static unsigned status(void) { return (channel[RA_SHARED_UNLOCK_PROGRESS] >> 8) & 255u; }

int main(void) {
    CHECK(RA_SHARED_UNLOCK_PROGRESS < CARDENGINE_SHARED_SLOTS);
    CHECK(RA_SHARED_UNLOCK_PROGRESS > RA_SHARED_UNLOCK_LEVEL);
    CHECK(sizeof(raUnlockDiagnostics) == 28);
    CHECK(offsetof(raUnlockDiagnostics, accepted) == 8);
    CHECK(offsetof(raUnlockDiagnostics, append_status) == 20);
    CHECK(CARDENGINEI_ARM9_RA_UNLOCK_LOCATION == CARDENGINEI_ARM9_RA_SESSION_LOCATION + 0x40);
    CHECK(0x40 + sizeof(raUnlockDiagnostics) <= CARDENGINEI_ARM9_RA_SESSION_MAX);
    CHECK(RA_UNLOCK_DIAGNOSTICS_VERSION == 1 && RA_UNLOCK_DIAGNOSTICS_MAGIC == 0x31554152u);
    reset();
    raUnlocksCluster = 0;
    CHECK(raUnlockAppend(100, raStampCache, 0) == RA_UNLOCK_NO_FILE);
    raUnlocksCluster = 1;
    readFail = 1;
    CHECK(raUnlockAppend(100, raStampCache, 0) == RA_UNLOCK_NO_FILE);
    readFail = 0;
    writeFail = 1;
    CHECK(raUnlockAppend(100, raStampCache, 0) == RA_UNLOCK_WRITE_FAILED);
    CHECK(raUnlockSlot == 0);
    writeFail = 0;
    CHECK(raUnlockAppend(100, raStampCache, 0) == RA_UNLOCK_OK);
    CHECK(raUnlockSlot == 1);
    reset();
    memset(disk, 'X', sizeof(disk));
    CHECK(raUnlockAppend(100, raStampCache, 0) == RA_UNLOCK_FULL);
    CHECK(writes == 0);
#ifdef TWLSDK
    reset();
    writeFail = 1;
    offer(100);
    CHECK(pending() == 1 && committed() == 0 && status() == RA_UNLOCK_WRITE_FAILED);
    CHECK(channel[RA_SHARED_UNLOCK_REQ] == RA_SHARED_UNLOCK_MAGIC);
    writeFail = 0;
    raUnlockService();
    CHECK(pending() == 0 && committed() == 1 && status() == RA_UNLOCK_OK);
    CHECK(channel[RA_SHARED_UNLOCK_REQ] == 0);
    puts("unlock TWL: failed append retains mailbox, retry commits once");
#else
    reset(); offer(100);
    sharedAddr[RA_SHARED_UNLOCK_ID] = 101;
    sharedAddr[RA_SHARED_UNLOCK_LEVEL] = RA_QUEUE_LEVEL_FULL;
    sharedAddr[RA_SHARED_UNLOCK_REQ] = RA_SHARED_UNLOCK_HARDCORE;
    raUnlockDrainExit();
    CHECK(pending() == 0 && committed() == 2 && writes == 2);
    CHECK(sharedAddr[RA_SHARED_UNLOCK_REQ] == 0);
    CHECK(strtoul(disk, 0, 10) == 100);
    CHECK(strtoul(disk + RA_QUEUE_RECORD, 0, 10) == 101);
    raUnlockDrainExit();
    CHECK(committed() == 2 && writes == 2);
    reset(); offer(100); readOngoing = 1;
    CHECK(raUnlockDrain() == RA_UNLOCK_READ_BUSY);
    CHECK(pending() == 1 && committed() == 0 && writes == 0 && status() == RA_UNLOCK_READ_BUSY);
    CHECK(raUnlockStateMagic == 0);
    CHECK(resumeCalls == 0);
    readCompletesAfter = 5;
    ime = 0;
    raUnlockDrainExit();
    CHECK(pending() == 0 && committed() == 1 && writes == 1 && status() == RA_UNLOCK_OK);
    CHECK(resumeCalls == 5 && !readOngoing && ime == 0);
    CHECK(strtoul(disk, 0, 10) == 100);
    reset(); offer(100); readOngoing = 1;
    raUnlockDrainExit();
    CHECK(pending() == 1 && committed() == 0 && writes == 0 && status() == RA_UNLOCK_READ_BUSY);
    CHECK(raUnlockStateMagic == 0);
    CHECK(resumeCalls == 263u * 60 && readOngoing && ime == 1);
    readOngoing = 0;
    raUnlockDrainExit();
    CHECK(pending() == 0 && committed() == 1 && writes == 1);
    puts("unlock exit: mailbox accepted once; read completion with IME off commits IDs; 60-frame timeout retains READ_BUSY without writes");
    for (int fault = 0; fault < 4; ++fault) {
        reset();
        offer(100);
        CHECK(pending() == 1 && committed() == 0);
        CHECK(channel[RA_SHARED_UNLOCK_REQ] == 0);
        if (fault == 0) memset(disk, 'X', sizeof(disk));
        if (fault == 1) raUnlocksCluster = 0;
        if (fault == 2) writeFail = 1;
        if (fault == 3) readFail = 1;
        raUnlockDrain();
        CHECK(pending() == 1 && committed() == 0);
        CHECK(status() == (fault == 0 ? RA_UNLOCK_FULL : fault == 2 ? RA_UNLOCK_WRITE_FAILED : RA_UNLOCK_NO_FILE));
        CHECK(raUnlockPending[0] == 100);
        if (fault == 0) { memset(disk, 0, sizeof(disk)); raUnlockStateMagic = 0; }
        raUnlocksCluster = 1; writeFail = readFail = 0;
        raUnlockDrain();
        CHECK(pending() == 0 && committed() == 1 && status() == RA_UNLOCK_OK);
    }
    reset();
    offer(100); offer(101);
    transientWrites = 3;
    raUnlockDrainExit();
    CHECK(pending() == 0 && committed() == 2 && writes == 5);
    CHECK(status() == RA_UNLOCK_OK);
    CHECK(strtoul(disk, 0, 10) == 100);
    CHECK(strtoul(disk + RA_QUEUE_RECORD, 0, 10) == 101);
    CHECK(disk[2 * RA_QUEUE_RECORD] == 0);
    reset(); offer(100); offer(101); offer(102); failOnWrite = 2;
    raUnlockDrainExit();
    CHECK(pending() == 0 && committed() == 3 && writes == 4);
    CHECK(strtoul(disk, 0, 10) == 100);
    CHECK(strtoul(disk + RA_QUEUE_RECORD, 0, 10) == 101);
    CHECK(strtoul(disk + 2 * RA_QUEUE_RECORD, 0, 10) == 102);
    CHECK(disk[3 * RA_QUEUE_RECORD] == 0);
    reset(); offer(100);
    transientReads = 3;
    raUnlockDrainExit();
    CHECK(pending() == 0 && committed() == 1 && writes == 1);
    reset(); offer(100); writeFail = 1;
    raUnlockDrainExit();
    CHECK(pending() == 1 && committed() == 0 && writes == 4);
    CHECK(status() == RA_UNLOCK_WRITE_FAILED);
    CHECK(prevSect[0] == -1 && prevSect[1] == -1);
    reset(); offer(100); raUnlocksCluster = 0;
    raUnlockDrainExit();
    CHECK(pending() == 1 && committed() == 0 && status() == RA_UNLOCK_NO_FILE);
    CHECK(criticalEnters == 4);
    reset(); offer(100); memset(disk, 'X', sizeof(disk));
    CHECK(raUnlockDrain() == RA_UNLOCK_FULL);
    criticalEnters = 0;
    raUnlockDrainExit();
    CHECK(criticalEnters == 1);
    CHECK(pending() == 1 && committed() == 0 && writes == 0);
    CHECK(status() == RA_UNLOCK_FULL);
    puts("unlock exit: three transient write/read retries commit once; persistent failure bounded to four attempts; FULL reported without retry");
    reset();
    for (u32 i = 1; i <= RA_UNLOCK_PENDING_MAX; ++i) {
        offer(i);
        CHECK(channel[RA_SHARED_UNLOCK_REQ] == 0);
        CHECK(pending() == i && committed() == 0);
    }
    offer(999);
    CHECK(channel[RA_SHARED_UNLOCK_REQ] != 0 && pending() == RA_UNLOCK_PENDING_MAX);
    raUnlockDrain();
    CHECK(committed() == RA_QUEUE_MAX && pending() == RA_UNLOCK_PENDING_MAX - RA_QUEUE_MAX);
    CHECK(status() == RA_UNLOCK_FULL);
    for (unsigned i = 0; i < pending(); ++i) CHECK(raUnlockPending[i] == i + RA_QUEUE_MAX + 1);
    for (int i = 0; i < RA_QUEUE_MAX; ++i) {
        CHECK((u32)strtoul(disk + i * RA_QUEUE_RECORD, 0, 10) == (u32)i + 1);
    }
    puts("unlock NTR: missing/full/read/write faults retain visible pending entries; retries commit once");
    puts("unlock NTR: 128 RAM unlocks, 64 SD commits, 64 retained in order, full-buffer backpressure");
#endif
    printf("unlock: PASSED (%u checks)\n", checks);
    return 0;
}
