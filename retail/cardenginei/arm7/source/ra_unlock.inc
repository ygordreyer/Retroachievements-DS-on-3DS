static u32 raUnlockStateMagic;
static u8 raUnlockSlot;
#ifndef TWLSDK
static u32 raUnlockPending[RA_UNLOCK_PENDING_MAX];
#endif
extern int prevSect[2];

static raUnlockStatus raUnlockAppend(u32 id, const char* stamp, int hardcore) {
	char record[RA_QUEUE_RECORD];
	char digits[11];
	u32  value = id;
	int  n = 0;
	int  i;

	if (raUnlocksCluster == 0 || id == 0) {
		return RA_UNLOCK_NO_FILE;
	}

	if (raUnlockStateMagic != RA_UNLOCK_STATE_MAGIC) {
		u8 slot;

		raUnlockStateMagic = RA_UNLOCK_STATE_MAGIC;

		prevSect[0] = -1;
		prevSect[1] = -1;

		for (slot = 0; slot < RA_QUEUE_MAX; slot++) {
			if (fileRead(record, &raUnlocksFile, slot * RA_QUEUE_RECORD, 1) != 1) {
                raUnlockStateMagic = 0;
                return RA_UNLOCK_NO_FILE;
            }
			if (record[0] == 0) {
				break;
			}
		}
		raUnlockSlot = slot;
	}

	if (raUnlockSlot >= RA_QUEUE_MAX) {
		return RA_UNLOCK_FULL;
	}

	while (value && n < (int)sizeof(digits)) {
		digits[n++] = (char)('0' + (value % 10));
		value /= 10;
	}
	toncset(record, 0, RA_QUEUE_RECORD);
	for (i = 0; i < n; i++) {
		record[i] = digits[n - 1 - i];
	}
	record[n] = '\n';

	if (stamp) {
		int at = n;

		record[at++] = '\t';
		for (i = 0; i < RA_QUEUE_STAMP; i++) {
			record[at++] = stamp[i];
		}

		record[at++] = '\t';
		tonccpy(record + at, raGameCode, RA_QUEUE_CODE);
		at += RA_QUEUE_CODE;
		record[at++] = '\t';
		tonccpy(record + at, raGameTitle, RA_QUEUE_TITLE);
		at += RA_QUEUE_TITLE;

		record[at++] = '\t';
		record[at++] = hardcore ? '1' : '0';
		record[at] = '\n';
	}

	if (fileWrite(record, &raUnlocksFile, raUnlockSlot * RA_QUEUE_RECORD, RA_QUEUE_RECORD) != RA_QUEUE_RECORD) {
        return RA_UNLOCK_WRITE_FAILED;
    }
	raUnlockSlot++;
    return RA_UNLOCK_OK;
}

static void raUnlockProgress(unsigned pending, raUnlockStatus status, unsigned commits) {
    sharedAddr[RA_SHARED_UNLOCK_PROGRESS] =
        ((sharedAddr[RA_SHARED_UNLOCK_PROGRESS] + (commits << 16)) & 0xFFFF0000u)
        | ((unsigned)status << 8) | pending;
}

static void raUnlockService(void) {
    const u32 req = sharedAddr[RA_SHARED_UNLOCK_REQ];
    if (req != RA_SHARED_UNLOCK_MAGIC && req != RA_SHARED_UNLOCK_HARDCORE)
        return;
    if (sharedAddr[RA_SHARED_UNLOCK_LEVEL] != RA_QUEUE_LEVEL_HANDOFF) {
#ifdef TWLSDK
        raUnlockStatus status;
        if (readOngoing) return;
        const int oldIME = enterCriticalSection();
        status = raUnlockAppend(sharedAddr[RA_SHARED_UNLOCK_ID], raStampCache,
                                req == RA_SHARED_UNLOCK_HARDCORE);
        raUnlockProgress(status != RA_UNLOCK_OK, status, status == RA_UNLOCK_OK);
        leaveCriticalSection(oldIME);
        if (status != RA_UNLOCK_OK) return;
#else
        const unsigned count = sharedAddr[RA_SHARED_UNLOCK_PROGRESS] & 255u;
        if (count >= RA_UNLOCK_PENDING_MAX) {
            raUnlockProgress(count, RA_UNLOCK_FULL, 0);
            return;
        }
        raUnlockPending[count] = sharedAddr[RA_SHARED_UNLOCK_ID]
            | (req == RA_SHARED_UNLOCK_HARDCORE ? RA_UNLOCK_HARDCORE_BIT : 0);
        raUnlockProgress(count + 1,
            (raUnlockStatus)((sharedAddr[RA_SHARED_UNLOCK_PROGRESS] >> 8) & 255u), 0);
#endif
    }
    sharedAddr[RA_SHARED_UNLOCK_REQ] = 0;
}

#ifndef TWLSDK
static bool resume_cardRead_arm9(void);

void raUnlockReceive(void) {
    raUnlockService();
}

static __attribute__((noinline)) raUnlockStatus raUnlockDrain(void) {
    unsigned count = sharedAddr[RA_SHARED_UNLOCK_PROGRESS] & 255u;
    unsigned done = 0;
    raUnlockStatus status = RA_UNLOCK_OK;
    int oldIME;
    if (!count) return RA_UNLOCK_OK;
    oldIME = enterCriticalSection();
    if (readOngoing) {
        raUnlockProgress(count, RA_UNLOCK_READ_BUSY, 0);
        leaveCriticalSection(oldIME);
        return RA_UNLOCK_READ_BUSY;
    }
    while (done < count) {
        const u32 packed = raUnlockPending[done];
        status = raUnlockAppend(packed & ~RA_UNLOCK_HARDCORE_BIT, raStampCache,
                                (packed & RA_UNLOCK_HARDCORE_BIT) != 0);
        if (status != RA_UNLOCK_OK) break;
        ++done;
    }
    count -= done;
    if (done && count)
        tonccpy(raUnlockPending, raUnlockPending + done, count * sizeof(u32));
    raUnlockProgress(count, status, done);
    leaveCriticalSection(oldIME);
    return status;
}

static __attribute__((noinline)) void raUnlockDrainExit(void) {
    unsigned retries = 3;
    raUnlockStatus status;
    if (readOngoing) {
        const int oldIME = enterCriticalSection();
        unsigned elapsed = 0;
        unsigned previous = REG_VCOUNT;
        /* VCOUNT advances with IME off; 60 frames allow about one second. */
        while (readOngoing && elapsed < 263u * 60) {
            unsigned current;
            resume_cardRead_arm9();
            current = REG_VCOUNT;
            elapsed += current >= previous ? current - previous : 263u + current - previous;
            previous = current;
        }
        leaveCriticalSection(oldIME);
    }
    do {
        raUnlockService();
        status = raUnlockDrain();
        if (status == RA_UNLOCK_OK && sharedAddr[RA_SHARED_UNLOCK_REQ]) {
            raUnlockService();
            status = raUnlockDrain();
        }
        if (status != RA_UNLOCK_WRITE_FAILED && status != RA_UNLOCK_NO_FILE) break;
        prevSect[0] = prevSect[1] = -1;
    } while (retries--);
}
#endif
