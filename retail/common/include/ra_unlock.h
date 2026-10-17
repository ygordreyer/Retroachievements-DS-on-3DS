#ifndef RA_UNLOCK_H
#define RA_UNLOCK_H
#include <stdint.h>

#define RA_SHARED_UNLOCK_PROGRESS 12
#define RA_UNLOCK_PENDING_MAX 128
#define RA_UNLOCK_HARDCORE_BIT 0x80000000u
#define RA_UNLOCK_DIAGNOSTICS_MAGIC 0x31554152u /* RAU1 */
#define RA_UNLOCK_DIAGNOSTICS_VERSION 1

typedef char raUnlockPendingFitsByte[(RA_UNLOCK_PENDING_MAX <= 255) ? 1 : -1];

typedef enum raUnlockStatus {
    RA_UNLOCK_OK = 0,
    RA_UNLOCK_FULL = 1,
    RA_UNLOCK_NO_FILE = 2,
    RA_UNLOCK_WRITE_FAILED = 3
} raUnlockStatus;

/* ARM9 owns mailbox ID/level and publishes REQ last; ARM7 clears REQ only on RAM acceptance,
 * owns progress (pending:8, result:8, committed:16), and ARM9 mirrors it into RAU1 for IGM. */
typedef struct raUnlockDiagnostics {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t accepted;
    uint32_t committed;
    uint32_t pending;
    uint32_t append_status;
    uint32_t refused;
} raUnlockDiagnostics;
#endif
