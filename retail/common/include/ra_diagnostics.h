#ifndef RA_DIAGNOSTICS_H
#define RA_DIAGNOSTICS_H

#include <stdint.h>

#define RA_DIAGNOSTICS_MAGIC 0x31444152u /* 'RAD1' */
#define RA_DIAGNOSTICS_VERSION 1

/* ARM9 RA writes the unused session tail; IGM reads while the game is paused.
 * Addresses are console addresses. rejected_reads saturates; other counters wrap modulo 2^32.
 * Active triggers are nonnull runtime entries, including disabled/terminal entries;
 * trigger_evaluations counts visits to that fixed roster since activation. */
typedef struct raRcDiagnostics {
    uint32_t magic;                    /* +0x00 */
    uint16_t version;                  /* +0x04 */
    uint16_t size;                     /* +0x06 */
    uint32_t disabled_count;           /* +0x08 */
    uint32_t first_disabled_id;        /* +0x0C */
    uint32_t first_disabled_address;   /* +0x10 */
    uint32_t rejected_reads;           /* +0x14 */
    uint32_t first_rejected_address;   /* +0x18 */
    uint32_t first_rejected_trigger;   /* +0x1C, 0 during the shared memref pass */
    uint32_t frames_evaluated;         /* +0x20 */
    uint32_t frames_not_evaluated;     /* +0x24, before activation or after stop */
    uint32_t trigger_evaluations;      /* +0x28 */
    uint32_t active_triggers;          /* +0x2C */
} raRcDiagnostics;

#endif
