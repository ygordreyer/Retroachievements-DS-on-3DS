# In-game achievement tracking design

Live unlock detection and a text Achievements menu already exist. Per-achievement measured progress and badge rendering are missing integration work. Add progress to the paused menu first, then badge cards, then throttled gameplay notifications.

The main progress blocker is the custom evaluator: vendored rcheevos supports progress and primed events, but the fork’s frame loop only dispatches triggered events. A progress callback alone will never run.

## Scope and evidence

Investigation date: 2026-10-18. The task supplied branch `topic/handheld-dev` and HEAD `d783e97`. No git commands were run to verify those identifiers.

- **Confirmed-by-code** means inspected source behavior, without device verification.
- **Proposal** means a design change, not implemented behavior.
- **Hypothesis** means compatibility or performance that requires measurement.

The original investigation changed no files and accessed no credentials, credential files, RA endpoints, network services, builds, or device tests. The companion viewer was read without edits. The source investigation below records the state before implementation; the Implemented section supersedes its corresponding gaps.

Repository paths below are relative to `nds-ra-3ds`. Viewer paths are relative to `../ra3ds-viewer`.

## Implemented

Stages 2, 3, 4, 6 and 7 implemented on 2026-10-18. Gameplay progress notifications, evaluation cadence, queue records and networking are unchanged.

- Paused query ABI: offsets 0/4/8/12 are preserved. Offset 16 branches to a read-only trigger query. Offsets 20/24/28/32 contain RAQ1 magic, version 1, query capability and result size 16. `ra_progress.h` defines the contract. The loaded-data guard rejects pre-initialization access without touching uninitialized BSS. Missing, disabled, unmeasured, unknown/initializing, measured, percentage and earned states are distinct. Earned entries omitted from the runtime keep an unknown target. Queries never evaluate conditions or read game memory.
- IGM: nine two-line entries, clipped titles, reserved points columns, measured bars and raw or percentage text. Unknown progress has a label, never a fabricated 0%. The header counts each earned entry once. A opens full progress details. Only visible rows and the selected detail issue queries while paused. Rendering uses the existing font, palette and backed-up tile map.
- Unlocks: session-earned and ring-refusal flags are independent of queue acceptance. A full ring still marks the achievement earned, increments the loss diagnostic, leaves the queued count unchanged and shows `Queue full: not saved`. Details also show disabled persistence and SD refusal/write failures. Server-earned and queued flags remain disjoint; the 64 by 48-byte SD queue format is unchanged.
- Host checks: `bash tools/ra_progress_test.sh` covers the ABI, immutable queries, formatting, clipping, bar math and ring refusal. It also checks the built ARM branch destinations, loaded guard, header mirror and binary limits when build artifacts exist. The existing reader harness now supports macOS through Rosetta and Mach-O absolute symbols. Its macOS fit measurement counts requested payloads aligned to eight bytes, matching the DS allocator, rather than macOS allocation size classes. Linux accounting and the existing 8,192-byte fit threshold are unchanged.

Measured with the user-space toolchain, `make RA_LAUNCHER_WIFI=2 'CC=arm-none-eabi-gcc -std=gnu17'`, exit 0:

| Resource | Before | After | Remaining |
|---|---:|---:|---:|
| IGM raw binary | 23,052 bytes | 24,932 bytes | See allocated ceiling below |
| IGM allocated through `__bss_end` | 33,508 bytes | 35,388 bytes | 4,548 of 39,936 bytes |
| IGM aligned text header | 3,968 bytes | 3,968 bytes | Layout unchanged |
| RA raw image | 79,184 bytes | 79,648 bytes | 51,424 of 131,072 bytes |
| RA static allocation through `__bss_end` | 92,688 bytes | 93,152 bytes | 136,224-byte allocator arena before definitions |

The existing real-set host fit fixture uses 128,016 bytes including block headers, leaving 8,208 bytes in that arena. This is a host measurement, not a device stack or timing measurement. `tools/ra_host_test.sh`, `tools/ra_reader_test.sh` and `tools/ra_snapshot_addr.sh` pass. No missing tools were installed.

Owner device checks, still untested:

1. Open the in-game menu with **L + Down + Select**, the default mask `0x0284` from `retail/arm9/source/conf_sd.cpp` (the ARM7 tests the configured `igmHotkey` in `retail/cardenginei/arm7/source/cardengine.c`). A launcher override can change this combination. Select RetroAchievements, then Achievements.
2. For a known measured achievement, check the numerator/target or percentage before and after playing. A opens details, B returns, Up/Down selects, and Left/Right pages. Confirm progress freezes while paused, resets correctly after resuming, and distinguishes measured zero from No progress data or Initializing.
3. Check long titles and the largest progress values: points stay visible, the last page has a valid cursor, and details fit within the screen. Trigger an achievement and confirm its star, earned total and completion percentage agree.
4. Test repeated early menu entry, normal unlock persistence, queue-disabled mode, burst unlocks, SD refusal/full queue and write failure. A refused unlock must remain earned for the session without claiming it was saved. Confirm SD commits and later server acknowledgement separately.
5. Repeat menu entry, paging, detail and resume on NTR and TWLSDK titles, including graphics transitions and save/load. Device readability, timing, stack margin, persistence and resume integrity remain unverified. Gameplay progress-popup work is excluded.

Paused badges (stages 6 and 7):

- The launcher knows the RA GameID in `verdict.gameId` after identification. It opens only `sd:/_nds/nds-bootstrap/ra/badges/<GameID>.rab`, read only. Before launch it checks the RABC v1 header, matching GameID, actual file length, count up to 4096, dimensions, format, flags, reserved words, contiguous directory/payload bounds and strictly sorted unique IDs. The 16-byte descriptor (magic, GameID, SD cluster, validated count) occupies the previously unused session bytes `+0x80` to `+0x8F`. The bootloader captures it in a local before clearing the buffered session and passes that local to both ARM7 hook calls. The hook copies it to the appended ARM7 header field at `+0xF0`, only with valid session/descriptor magic and SD support. Missing session magic leaves a zero descriptor. This covers DS and DSi-enhanced retail without reading the runtime WRAM after it returns to ARM9 ownership. The DSiWare hook receives the same captured value, but its cardengine has no RA runtime or `BADG` handler. The B4DS bootloader also has no RA badge path. Existing header offsets stay fixed.
- Shared FAT reads now return exactly the requested length after copying a final partial sector. End-of-chain returns retain the actual copied count. The `fileRead` caller audit covers retail, homebrew, B4DS, bootloaders and cardengines, including `ra_unlock.inc`: the badge adapter consumes an exact byte count, and unlock slot checks require a one-byte result. One-byte reads never reach the faulty final-sector accounting, and the host test also checks their counts. Other uses discard the result or convert it to a boolean. None depends on the previous over-count.
- ARM7 services `BADG` only in the paused menu command loop, serialized with manual/page I/O. Request words 0/1 carry achievement ID and destination; completion returns status in word 0 and payload CRC in word 2. Words 9 to 12 and the unlock protocol are untouched. Binary search reads at most 13 directory entries of 32 bytes and one 544-byte payload per hit. It rechecks the selected offset/size and short reads. No whole-pack WRAM allocation, filename lookup while paused, or badge write/create/delete is used. The pack's cluster/layout must remain unchanged until the game exits, as with the existing prepared manual descriptor.
- A cache-line-aligned 544-byte IGM buffer receives the read. ARM7 refuses destinations outside the fixed IGM allocation or without alignment. ARM9 flushes before the transfer, invalidates after completion, then checks the payload CRC, transparent palette index 0 and RGB555 range before any VRAM upload. Missing, invalid, absent-ID, short-read and placeholder results draw a neutral crossed square. The exporter checks the whole-content CRC during preparation; the DS checks individual payloads before use.
- Nine two-line rows remain. A selected-achievement preview follows the caret in map columns 27 to 30, starting at the selected row and ending no lower than map row 22. It uses one 32 by 32 badge slot. The detail view places the same badge at `(27,19)` to `(30,22)`. Both placements avoid cursor columns 0 and 31, including the RTL cursor's full-column clearing. Nine full badges cannot fit beside 16-pixel-high rows. Titles have 18 columns and points keep five. The progress bar starts at column 2, with its text ending before column 27 even for maximum 32-bit numerator/target values. Earned badges use color; locked palettes use rounded `(77R + 150G + 29B + 128) / 256` in RGB555 space.
- The selected ID, including a failed result, is cached for the page visit. Navigation reads only when the selected ID changes. Opening/closing details reuses the same image. Badge tiles use `0x2000` to `0x21FF`, palette bank 6, and only the existing 1536-byte visible map. Font/tile backup grows from 8192 to 8704 bytes. The existing complete 512-byte palette and visible map backups cover every badge write; all are restored at menu exit. No game scratch outside the swapped IGM allocation is borrowed.
- Offline identity limit: the existing hash-keyed definitions cache does not retain GameID. A boot that cannot identify this ROM has no prepared badge descriptor and shows placeholders, even if its definitions and badge file exist. This change adds no identity sidecar or badge-file writes. Missing or rejected descriptors remain inactive until the next launch.
- `tools/ra_badge_test.sh` uses the copied, unmodified viewer golden pack and ASan/UBSan. It covers malformed headers/directories, wrong game, payload CRC, truncation and short reads, sorting, out-of-range offsets, placeholder/missing ID, all 32768 grayscale inputs, DS tile layout, touched-byte containment/restoration, maximum progress-number fit, binary descriptor offsets, aligned staging and linker limits. It extracts the production `fileRead` function and runs it in single-card and two-card configurations with cached/uncached and contiguous/fragmented chains: offsets 160, 704 and 1248 each return 544 bytes, and end-of-chain reads return only copied bytes. It also runs the production cursor for both RTL values against all list/detail badge positions and the production progress renderer against the column limit. Source checks pin the descriptor capture before session clearing and its use by both hook calls. A 4096-entry synthetic pack checks the worst-case search bound. Only the badge reader uses size optimization on ARM7; gameplay code optimization flags are unchanged. Launch validation and IGM CRC checking keep the TWLSDK image within its original ceiling.

Badge build measurements, same build command as above:

| Resource | Raw image | Allocated through `__bss_end` | Limit | Remaining |
|---|---:|---:|---:|---:|
| IGM | 25,860 bytes | 37,400 bytes (was 35,388) | 39,936 bytes | 2,536 bytes |
| ARM7 NTR and alternate | 48,160 bytes each | 51,396 bytes each | 62,464 bytes each | 11,068 bytes each |
| ARM7 TWLSDK and TWLSDK3 | 31,076 bytes each | 33,740 bytes each | 33,792 bytes each | 52 bytes each |

Full build and all five host scripts exit 0 after the descriptor handoff, FAT count and RTL layout fixes. IGM header remains 3,968 aligned bytes. RA runtime allocation and allocator margin are unchanged. The 52-byte TWLSDK figure is linker space, not a measurement of the game's interrupt stack. SHA-256 of `retail/bin/nds-bootstrap.nds`: `e5fc941b0c382c9ca5a3022117823ea1ab0bb0fdde8257162f2e6a9bb3020d7f`.

Owner badge checks, all still untested:

1. Prepare the running game's pack in the 3DS viewer, then launch with successful game identification. Open RetroAchievements, then Achievements with L + Down + Select. Check badge readability, transparency, selected preview association, clipped titles, points, bars and full details, including Arabic and Hebrew cursor placement. Locked badges should be gray; earned badges should be colored.
2. Hold Up/Down, page Left/Right rapidly, open/close details repeatedly, then close/reopen the IGM. Reads should follow selection changes, with no continual per-frame I/O. Check the final partial page and early menu entry before the viewer/runtime initializes.
3. Test missing, wrong-game, corrupted, truncated and placeholder packs, an absent achievement ID, and an offline boot without GameID. Every case should show neutral placeholders without freezing or claiming a successful image.
4. On both NTR and TWLSDK, repeat navigation/resume across graphics transitions, save/load and streamed gameplay with fragmented and full SD media. Verify every touched tile/map/palette byte is restored, SD saves and queue commits remain intact, and no badge file is written. Measure menu latency and ARM7 stack margin, especially the small TWLSDK link margin below.

## Current-state map

| Component | Confirmed-by-code behavior | Evidence |
|---|---|---|
| Game evaluation | ARM9 calls the separate WRAM runtime, which evaluates achievements and then ticks the overlay. | `retail/cardenginei/arm9/source/ra_reader.c:98`, `:143`; `retail/cardenginei/arm9_ra/source/cardengine.c:537`, `:553` |
| Frame semantics | The current custom loop updates memrefs and visits every trigger each evaluated frame. The older report’s sliced-evaluation findings do not describe this implementation. | `retail/cardenginei/arm9_ra/source/ra_rc_frame.c:21`, `:67`, `:69` |
| Unlock event | Increments trigger count, queues the ID, finds its staged title, renders the notification, and schedules the beacon. | `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:728` |
| Gameplay notification | Two right-aligned text lines, white ink with black shadow. Normally “RA UNLOCKED” plus the achievement title. No badge image or progress bar. | `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:298`, `:782`; `retail/cardenginei/arm9_ra/source/ra_text.c:234` |
| Popup geometry | A 256×16, 4bpp strip. Eight 32×16 sprites when possible, otherwise a borrowed background layer. Top screen coordinates within the sub engine, row 1. | `retail/common/include/ra.h:972`; `retail/cardenginei/arm9_ra/source/ra_overlay.c:182`, `:778`, `:994`, `:1104` |
| Duration | 180 overlay ticks. Brightness fades defer up to 90 eligible ticks; forced blank/display-off waits without spending that fade bound. | `retail/cardenginei/arm9_ra/source/ra_overlay.c:190`, `:205`, `:1439` |
| Popup multiplicity | A count and one mutable text strip drive notifications, not an achievement notification FIFO. Closely spaced unlocks can coalesce. | `retail/cardenginei/arm9_ra/source/ra_overlay.c:1415`, `:1457`; `retail/cardenginei/arm9_ra/include/ra_text.h:42` |
| In-game RA menu | Root RetroAchievements entry contains Sync Pending and Achievements. The Achievements list has 19 rows, paging, title, points, earned markers, overall completion percentage, and an A-button detail page. | `retail/cardenginei/arm9_igm/source/inGameMenu.h:15`; `retail/cardenginei/arm9_igm/source/inGameMenu.c:729`, `:786`, `:805`, `:894`, `:1058` |
| Achievement details | Description, points, earned/sync-pending status, and earned date when available. No per-achievement measured progress or badges. | `retail/cardenginei/arm9_igm/source/inGameMenu.c:898` |
| Data access | IGM directly reads a fixed WRAM viewer index and string offsets. It does not reconstruct the mutated definitions text or access the static runtime object. | `retail/cardenginei/arm9_igm/source/inGameMenu.c:730`; `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:886` |

### Popup enablement

**Confirmed-by-code:** the shipped configuration default is level 1 with an empty allowlist. Consequently, normal launcher-staged sessions have no gameplay popup until a game is explicitly listed.

| Setting | Meaning |
|---|---|
| `overlay=0` | No overlay survey or drawing. |
| `overlay=1` | Draw only for RA GameIDs in `overlay_games`. Default. |
| `overlay=2` | Attempt drawing for every game. |
| `overlay_games=…` | Numeric RA GameID allowlist, parsed by the launcher. Unknown GameID 0 never matches. |

Evidence: `retail/arm9/source/ra_cfg.c:174`, `:294`; `retail/common/include/ra_wifi.h:678`; `retail/arm9/source/ra_wifi.c:2466`.

The launcher resolves this policy to the session block’s boolean `overlay`. If that block has no valid magic, the runtime currently permits overlay attempts. Therefore “disabled by default” describes the normal staged launch path, not every possible unstaged diagnostic launch.

Evidence: `retail/cardenginei/arm9_ra/source/ra_overlay.c:1182`.

`queue` and `submit` govern separate parts of unlock persistence and submission. They must not become progress-popup controls.

### Measured snapshot

**Confirmed-by-code:**

1. Each tick calls `rc_runtime_get_achievement_measured()` for `defIds[0]`, or the synthetic fallback ID.
2. It captures that trigger’s state, measured value, and target.
3. `rcMeasured` and `rcTarget` update only when target is nonzero.
4. The values therefore retain the last active reading after the trigger fires.
5. This is diagnostic state for one staged definition, not a list of progress values.

Evidence: `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:1623`; snapshot offsets are `+0x74` and `+0x78` in `retail/common/include/ra.h:537`.

Vendored rcheevos returns zero measured values for inactive triggers. The retained snapshot can consequently stop at 599/600 after an unlock. It is not suitable as the menu’s authoritative completion display.

Evidence: `retail/cardenginei/arm9_ra/rcheevos/src/rcheevos/runtime.c:237`; `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:1641`.

There is also an identification trap: the local `firstId` used for measured lookup shadows the file-static `firstId`. `snapshot->rcFirstId` receives the file-static first-triggered ID, which can differ from the achievement supplying `rcMeasured`.

Evidence: `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:225`, `:766`, `:1630`, `:1640`.

Consumers found are host assertions and diagnostic documentation. The IGM does not consume `rcMeasured` or `rcTarget`; its RAM viewer can expose the snapshot bytes manually. The snapshot itself lives in the ARM9 cardengine’s `raSnapshotBuffer`, not in the fixed viewer block.

Evidence: `retail/cardenginei/arm9/source/ra_reader.c:44`; `tools/ra_reader_test.c:877`, `:2014`.

### IGM entry, drawing, and restoration

**Confirmed-by-code:** launcher default hotkey hexadecimal `284` is L + Down + Select. ARM7 checks the configured mask, menu accessibility, Wi-Fi IRQ exclusion, and the save mutex before entering.

Evidence: `retail/arm9/source/conf_sd.cpp:635`; `retail/cardenginei/arm7/source/cardengine.c:2148`.

ARM9 enters a critical section and calls the IGM after the ARM7 loading handshake. ARM7 remains in a menu service loop, reading input and executing explicit commands. Normal game evaluation is paused.

Evidence: `retail/cardenginei/arm9/source/cardengine.c:1557`; `retail/cardenginei/arm7/source/inGameMenu.c:60`, `:140`.

| Resource | Existing handling |
|---|---|
| IGM code location | `0x02F88000`; ARM7 swaps a 40 KiB region with the page file on entry and exit. |
| Linker ceiling | 39 KiB, or 39,936 bytes, including allocated sections. This is a limit, not a measured current binary size. |
| Font | 256 glyphs, 8×8 pixels, 2,048 bytes at 1bpp, expanded to 8,192 bytes at 4bpp. |
| Text renderer | BG3 tile map, screen base 15, character base 0, 32-column layout. Character, string, aligned string, decimal, and hexadecimal helpers. |
| Colors | White, light gray, darker gray, light blue, red, lime; black background. |
| Borrowed VRAM | Maps bank H as sub-background VRAM and temporarily unmaps C when appropriate. |
| Backups | 8,192 bytes of font-area VRAM, 1,536 bytes of visible tile map, and all 512 bytes of sub-background palette. |
| Register restoration | Restores sub display control, BG control registers, VRAM C/H controls, brightness, and display power state. |
| Existing limitations | Sets BG3 scroll to zero and clears mosaic/blending registers. These are not all restored by this function. Do not describe the current menu as a complete GPU-state snapshot. |

Evidence: `retail/common/include/locations.h:58`; `retail/cardenginei/arm7/source/cardengine.c:1285`; `retail/cardenginei/arm9_igm/cardengine.ld.in:9`; `retail/common/include/igm_text.h:27`; `retail/cardenginei/arm9_igm/source/inGameMenu.c:140`, `:1563`, `:1598`, `:1770`; `retail/cardenginei/arm9_igm/source/card_engine_header.s:53`.

The visible backup arrays plus the ARM7 RAM-viewer backup total 10,432 bytes. `IgmText` occupies 3,964 bytes, aligned to 3,968. Actual compiled code size and remaining link margin are **unmeasured** because no ELF/MAP artifacts were present and no build was performed.

An earlier header-layout failure is documented in source: changing `IgmText` without matching assembly `.space` shifted the entry point and crashed menu entry. Preserve the header contract and run its existing symbol-offset check.

Evidence: `retail/common/include/igm_text.h:37`; `retail/cardenginei/arm9_igm/source/card_engine_header.s:27`; `tools/ra_release.sh:67`.

## Runtime data and memory

### Fixed WRAM layout

**Confirmed-by-code**, with addresses calculated from `locations.h`:

| Address/range | Size | Owner/use |
|---|---:|---|
| `0x03740000` to `0x0377FFFF` | 256 KiB | Complete RA WRAM window. |
| `0x03778000` to `0x0377FFFF` | 32 KiB | Definitions reservation, including metadata blocks below. |
| `0x0377F300` | 256 bytes | Session reservation, including diagnostics/unlock diagnostics subregions. |
| `0x0377F400` | 2,560 bytes | Viewer reservation. |
| `0x0377FE00` | 512 bytes | Pending reservation. |
| Definitions text | 29,431 bytes maximum | Excludes reservations, 8-byte header, and final NUL. |
| Aligned `__bss_end` to `0x03778000` | Build-dependent | RA allocator arena. |

Evidence: `retail/common/include/locations.h:146`, `:188`, `:222`, `:273`, `:322`; `retail/arm9/source/ra_wifi.c:1683`; `retail/cardenginei/arm9_ra/source/startup.c:129`; `retail/cardenginei/arm9_ra/source/cardengine.c:468`.

The viewer currently needs 2,060 bytes: a 12-byte header plus 128 entries of 16 bytes. Its reservation has 500 bytes spare.

Increasing this reservation within the existing definitions window reduces definitions text capacity. It does not directly lower the allocator’s fixed upper boundary. Comments suggesting otherwise should not replace the actual address arithmetic.

The RA runtime also owns an 8 KiB evaluation stack in its static storage.

Evidence: `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:132`.

### What the IGM can read today

| Information | Availability and limitation |
|---|---|
| Achievement ID | `raViewerEntry.id`. |
| Title | Offset into definitions text; launcher keeps at most 31 bytes. |
| Description | Offset into definitions text; launcher keeps at most 64 bytes. |
| Points | Offset to a decimal string. |
| Earned before this session | `RA_VIEWER_EARNED` for server-earned records; `RA_VIEWER_QUEUED` for locally queued records matched at boot. |
| Earned during this session | Unlock-ring acceptance updates `RA_VIEWER_QUEUED` and the viewer count. |
| Earned timestamp | Packed value where known. Normally absent for a new in-session unlock. |
| Measured value/target | Stored in allocated rcheevos triggers, not in `raViewerEntry`. No IGM query interface today. |
| Measured display mode | Trigger has `measured_as_percent`; unavailable through the current viewer index. |
| Badge identifier or pixels | Neither is present in the viewer entry. |
| Entire server set | Not guaranteed. Viewer has 128 entries, active-definition array has 128 slots, and definitions text can fill earlier. |

Evidence: `retail/common/include/ra_wifi.h:235`, `:284`, `:999`, `:1012`; `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:265`, `:886`, `:979`, `:1029`; `retail/cardenginei/arm9_ra/rcheevos/include/rc_runtime_types.h:278`.

The index records offsets while splitting records in place. Armed and earned records both enter the viewer. Earned records do not need a memaddr condition.

Under space pressure, armed records lose description, then points, then title before losing their condition. Earned records lose timestamp, description, points, then title before the record is dropped.

Evidence: `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:930`, `:1064`; `retail/arm9/source/ra_patch.c:253`, `:463`.

**Confirmed risk:** viewer updates occur after the full-ring early return. If the ring is full, an achievement can trigger and produce a notification without gaining its viewer earned marker.

Evidence: `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:536`, `:552`.

Minimal correction: separate “earned this session” UI state from persistence acceptance, while continuing to display queue refusal or loss honestly.

### Shared mailbox

Shared blocks are at `0x027FFA0C` or `0x02FFFA0C`. They have exactly 13 words. Unlock handling uses slots 9 through 12. Adding slot 13 would overwrite the unpatched-function table.

Evidence: `retail/common/include/locations.h:331`; `retail/common/include/ra.h:238`, `:298`; `retail/common/include/ra_unlock.h:5`.

ARM7 does not map the ARM9 RA WRAM window. A badge read must target ARM7-visible staging memory, followed by ARM9 copying to VRAM. Do not supply a WRAM runtime address as an ARM7 transfer destination.

Evidence: `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:684`.

## Progress integration

### Available rcheevos behavior

**Confirmed-by-code:** vendored changelog identifies version 12.4.0. Its runtime API provides:

- `RC_RUNTIME_EVENT_ACHIEVEMENT_PROGRESS_UPDATED`
- `RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED`
- `RC_RUNTIME_EVENT_ACHIEVEMENT_UNPRIMED`
- `RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED`
- Measured lookup and formatting helpers.

Evidence: `retail/cardenginei/arm9_ra/rcheevos/CHANGELOG.md:1`; `retail/cardenginei/arm9_ra/rcheevos/include/rc_runtime.h:91`, `:104`.

The reference frame implementation emits progress when the measured value changes, the previous value is known, the target is nonzero, and the trigger is in an eligible state. Percentage-mode events carry a percentage and fire only when its integer percentage changes. Other events carry the raw measured value.

Evidence: `retail/cardenginei/arm9_ra/rcheevos/src/rcheevos/runtime.c:555`.

**Root cause:** this fork calls its own `ra_rc_do_frame()` through `ra_rc_frame_run()`. That loop dispatches only triggered transitions. Other transitions are counted. It has no measured-change event path.

Evidence: `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:1533`; `retail/cardenginei/arm9_ra/source/ra_rc_frame.c:79`, `:94`; contract at `retail/cardenginei/arm9_ra/include/ra_rc_frame.h:30`.

### Recommended first interface

**Proposal:** add a versioned, capability-checked paused query entry to the WRAM binary.

- Preserve existing entry, flags, paused-offer entry, and magic at offsets 0, 4, 8, and 12.
- Add an explicit query entry and capability contract.
- Accept an achievement ID and return a small fixed result: ID, value, target, state/flags.
- Report initializing, missing, disabled, unmeasured, measured, and earned distinctly.
- Query only the visible page or selected detail while paused.
- Read existing triggers. Do not reevaluate conditions or read game memory during queries.
- Respect `measured_as_percent`; compute any displayed percentage with overflow-safe arithmetic.
- Show earned state without inventing a measured target for achievements omitted from the active runtime.

The existing paused-offer entry demonstrates the cross-binary pattern.

Evidence: `retail/cardenginei/arm9_ra/source/card_engine_header.s:16`; `retail/cardenginei/arm9_igm/source/inGameMenu.c:1615`.

This avoids a full per-achievement progress mirror and adds no gameplay scan. A full mirror containing two extra 32-bit values per entry would require 1,024 bytes, exceeding the viewer’s 500-byte spare space.

### Throttled gameplay progress

**Proposal:**

1. Restore progress/primed event parity in the custom frame loop, preserving single-frame memref and condition evaluation.
2. In callbacks, record a small candidate. Do not render, perform SD I/O, or allocate.
3. After evaluation, select at most one progress notification.
4. Initially require a meaningful upward change, a global cooldown, and a per-achievement cooldown. Suggested starting values: 5 percentage points, 120 ticks globally, 300 ticks per achievement.
5. Give unlocks priority. Expire stale progress candidates after resets or unlocks.
6. Render two lines such as achievement title and `45/100 (45%)`, using the existing text footprint and overlay safety policy.
7. Keep primed indicators optional. Primed means ready-to-trigger conditions, not completion percentage.

Cost is **unmeasured**. Extra comparisons in the existing trigger loop are bounded, but percent division, ID lookup, text rendering, and VRAM uploads still cost time. Avoid repeated linear ID searches for every progress event and avoid formatting every frame.

The evaluator already measures cost and stops after 30 consecutive overruns. Preserve that behavior, and measure total reader-plus-overlay time as well.

Evidence: `retail/cardenginei/arm9_ra/source/ra_rc_frame.c:124`; `retail/cardenginei/arm9_ra/include/ra_rc_frame.h:8`; `retail/cardenginei/arm9/source/ra_reader.c:161`.

**Required safety change:** notification payloads need independent lifetimes. The renderer returns one mutable 2 KiB strip, and the overlay retains its pointer. Rendering progress into that strip can change an unlock already visible or deferred. Use a separate frozen display strip or equivalent immutable snapshot.

Evidence: `retail/cardenginei/arm9_ra/include/ra_text.h:42`; `retail/cardenginei/arm9_ra/source/ra_overlay.c:1046`, `:1139`.

## Badge feasibility

### Recommended representation

**Proposal:** 32×32, 4bpp DS-tiled pixels with a separate 16-color RGB555 palette per badge.

- 512 pixel bytes plus 32 palette bytes per badge.
- Index 0 transparent; 15 visible colors.
- Viewer downsamples and quantizes the source PNG.
- Store the colored badge once. Produce a grayscale palette for locked display.
- Retain badges as decorative data, separate from account unlock state.
- Use placeholders when unavailable.

A 24×24 version saves space but loses detail. An 8bpp version consumes substantially more palette memory. RGB555 is straightforward for a bitmap renderer, but the current IGM is a tiled text renderer.

The stated 64×64 PNG source size is task input, not independently verified for every badge. The converter must validate actual dimensions.

| Format, including per-image palette | 100 achievements | 300 achievements |
|---|---:|---:|
| 24×24, 4bpp | 32,000 bytes | 96,000 bytes |
| **32×32, 4bpp** | **54,400 bytes** | **163,200 bytes** |
| 32×32, 8bpp | 153,600 bytes | 460,800 bytes |
| 32×32, RGB555 | 204,800 bytes | 614,400 bytes |

These are calculated payload sizes, excluding file headers/directories. A 300-entry badge pack does not imply that the current runtime can evaluate or list 300 achievements.

### Where images can live

| Candidate | Assessment |
|---|---|
| SD, read while IGM is open | Preferred. Existing ARM7 menu commands already read page/manual data while the game is paused. Add a bounded badge-read command and explicit file descriptor. |
| IGM VRAM | Preferred display cache. Keep only visible badges, for example four 32×32 images, requiring 2 KiB of tile data. Back up and restore every additional byte touched. |
| RA WRAM heap | Avoid a full badge set. It competes with rcheevos parsing, triggers, code, and stack. Even 100 recommended badges cost over 53 KiB. |
| `INGAME_MENU_EXT_LOCATION`, `0x026B8000` | Borrowable only under a backup/restore protocol. Screenshot/manual code already saves affected memory to the page file. It is not permanently free storage. |
| Extended RAM ROM cache | Possible only after reserving space in every cache/preload/ROM-in-RAM path. An apparently unused address is not ownership. Defer this option. |
| Gameplay VRAM | Existing heuristic borrowing only. Do not enlarge the live popup for badges in the first implementation. |

Evidence: `retail/cardenginei/arm7/source/inGameMenu.c:187`; `retail/cardenginei/arm7/source/cardengine.c:1155`, `:1190`; `retail/common/include/locations.h:58`.

Relevant cache definitions:

| Mode/region | Start | Size |
|---|---|---:|
| Ordinary ROM/cache base | `0x0C3EC000`, alternate `0x0C400000` | DSi `0xBE0000`; 3DS `0x1BE0000` |
| DS title in DSi mode | `0x0C800000` | DSi `0x7FF000`; 3DS `0x17FF000` |
| 3DS TWL SDK cache | `0x0D000000` | `0x1000000`; cheats `0xFFBC00`; ROM-in-RAM limit `0xFE0000` |
| Small 3DS TWL reserve | `0x0DFE0000` | `0x20000`, or `0x1BC00` with cheats |
| Retail TWL alternatives | `0x02F60000`, `0x02F00000`, `0x02D00000` | `0x20000`, `0x80000`, `0x280000`, with smaller cheat variants |

Evidence: `retail/common/include/locations.h:362`.

The loader selects those regions and constructs adjusted cache-address tables. These are nominal configuration sizes, not guaranteed contiguous free allocations.

Evidence: `retail/bootloaderi/source/arm7/hook_arm9.c:335`, `:365`, `:436`, `:452`.

RA already reserves the top 256 KiB of DSi WRAM and conflicts with the color LUT. The lower WRAM area serves other bootstrap uses.

Evidence: `retail/common/include/locations.h:309`.

### Paused-menu rendering budget

**Proposal:** retain BG3 text and use four badge slots within the existing 4bpp tile address space.

- Font occupies offsets `0x0000` through `0x1FFF`.
- Put badge tiles at a separately reserved offset beginning at `0x2000`.
- Four badges consume `0x800` bytes.
- Existing visible map starts at offset `0x7800`.
- Allocate four palette banks outside the six existing text banks.
- Add 2 KiB of backup for overwritten badge tiles, or use explicitly protected external scratch.
- Existing palette backup already covers all 256 entries.
- Keep map changes within the backed-up visible region unless expanding its backup too.

**Hypothesis:** this fits the menu renderer cleanly. Current IGM link headroom is unknown, so the additional backup must be measured before choosing static storage.

No bitmap framebuffer is needed for the initial polished UI. Four badge cards, selected-row highlighting, measured bars, clipped titles, and a detail page can use tiles.

## Proposed SD badge cache

**Proposal:** `sd:/_nds/nds-bootstrap/ra/badges/<GameID>.rab`

Little-endian integers, fixed v1 dimensions, no PNG decoding or compression on DS.

### Header, 64 bytes

| Offset | Field |
|---|---|
| `0x00` | Four bytes `RABC` |
| `0x04` | `u16 version = 1` |
| `0x06` | `u16 header_bytes = 64` |
| `0x08` | `u32 game_id` |
| `0x0C` | `u32 entry_count` |
| `0x10` | `u16 width = 32`, `u16 height = 32` |
| `0x14` | `u16 format = 1` for DS tiled 4bpp; `u16 flags` |
| `0x18` | `u32 directory_offset = 64` |
| `0x1C` | `u32 directory_entry_bytes = 32` |
| `0x20` | `u32 data_offset` |
| `0x24` | `u32 file_bytes` |
| `0x28` | `u32 content_crc32` over directory and payload |
| `0x2C` | `u32 generation`, a local cache revision |
| `0x30` | 16 reserved zero bytes |

### Directory entry, 32 bytes, sorted by achievement ID

| Offset | Field |
|---|---|
| `0x00` | `u32 achievement_id` |
| `0x04` | `u32 payload_offset` |
| `0x08` | `u32 payload_bytes = 544` |
| `0x0C` | `u32 payload_crc32` |
| `0x10` | `u32 flags`, including missing-image placeholder |
| `0x14` | 12 reserved zero bytes |

Each payload contains 16 little-endian RGB555 palette words followed by 512 pixel bytes. Tiles are row-major, with four tiles per row. Within a tile, rows are consecutive and the left pixel occupies the low nibble.

All payload starts are 32-byte aligned because header, directory entries, and payload sizes are multiples of 32.

| Entries | Complete v1 file size |
|---:|---:|
| 100 | 57,664 bytes, 56.3125 KiB |
| 300 | 172,864 bytes, 168.8125 KiB |

DS validation must check version, dimensions, format, actual file length, bounded count, sorted unique IDs, overflow-safe offsets, payload lengths, and per-image CRC before display. Validate the whole-file CRC during preparation or explicit integrity checking, rather than rereading the entire pack for each icon.

Write a temporary file, close and verify it, then rename it into place. A stale or malformed badge cache must only cause placeholders.

The launcher should choose the file using the recognized GameID and stage a bounded cluster/size descriptor. Define its handoff storage explicitly. There is no existing badge-file descriptor or badge-read opcode to reuse unchanged.

## What the viewer must add

**Confirmed-by-code:** the inspected viewer has already moved beyond the brief’s HTTP-service description. Its current network code uses libcurl with HTTPS-only protocols, certificate verification, hostname verification, and a CA bundle. Its Makefile links mbedTLS.

Evidence: `../ra3ds-viewer/source/net.c:77`; `../ra3ds-viewer/Makefile:19`.

Existing pieces:

| Piece | Evidence |
|---|---|
| Achievement ID, badge name, title, description, points | `../ra3ds-viewer/source/api.c:115` |
| HTTPS badge URL generation | `../ra3ds-viewer/source/api.c:57` |
| PNG decode to RGBA | `../ra3ds-viewer/source/textures.c:23` |
| GPU-specific texture conversion | `../ra3ds-viewer/source/textures.c:37` |
| Temporary-file cache replacement pattern | `../ra3ds-viewer/source/cache.c:48` |

**Proposal:** add an explicit “prepare for DS gameplay” operation that enumerates the whole set, fetches all required badges, converts them to the DS cache format, and writes the pack.

Current texture requests are driven by drawing. Browsing some achievements does not prove every badge has been downloaded.

Evidence: `../ra3ds-viewer/source/textures.c:82`.

The exporter must use decoded RGBA, not the current Morton-ordered 3DS GPU texture. Add bounded image conversion independent of UI drawing, deterministic palette generation, progress/cancel status, and pack completeness checks.

Do not write account-earned flags into the badge pack. The DS runtime’s current account/session state remains authoritative.

### DS launcher HTTP alternative

**Confirmed-by-code:** the DS transport uses port 80 and supports caller-specified HTTP hosts. Vendored rcheevos defines both HTTP and HTTPS media host strings.

Evidence: `retail/common/include/ra_wifi.h:832`; `retail/arm9/source/ra_net.c:175`, `:260`, `:617`; `retail/cardenginei/arm9_ra/rcheevos/src/rapi/rc_api_common.c:12`.

**Unverified:** whether the media server currently serves PNGs over plain HTTP without redirecting to HTTPS. Host string constants do not prove endpoint behavior. No request was made because network access was prohibited.

The launcher alternative also needs badge-name retention, binary downloading, PNG decoding, scaling, and quantization. The viewer already has the relevant HTTPS and PNG infrastructure, so viewer conversion is the preferred implementation.

## Ranked implementation plan

Stages 2, 3, 4, 6 and 7 are implemented as recorded above; other rows remain proposals. Memory figures below are original design estimates, not measured compiled-code growth.

| Stage | Files/functions | Change | Memory cost | Risk | Host test possible | Device test needed |
|---|---|---|---|---|---|---|
| 1. Establish working gameplay baseline | Existing runtime, queue, IGM, diagnostic tools | Run first named real unlock with overlay off. Verify menu marker, SD commit, and later server acknowledgement separately. | No feature allocation | Existing compatibility unknown | Yes, existing harnesses; device acceptance cannot be replaced | Required before UI expansion |
| 2. Add paused measured query | `arm9_ra/source/card_engine_header.s`, `ra_rcheevos.c`, `common/include/locations.h`, small ABI header | Add capability-checked query entry returning existing trigger state, value, target, display mode. Preserve old fixed offsets. | About 16 bytes per query result; no full mirror | Cross-binary ABI and initialization guards | Yes, absent runtime, bad IDs, unmeasured, percent, disabled, triggered | Menu entry before/after initialization; repeated resume |
| 3. Display measured progress | `arm9_igm/source/inGameMenu.c`, `raAchievementsPage()` | Add detail progress and visible-row bars. Distinguish missing progress from 0%. Clip text to avoid overwriting points. | Small transient result and formatting buffers | Tight IGM size | Yes, formatting and boundary tests | Fonts, readability, controls, resume integrity |
| 4. Separate earned UI state from queue acceptance | `arm9_ra/source/ra_rcheevos.c`, `ra_rc_queue_unlock()` | Mark session-earned state even if persistence refuses; preserve refusal diagnostics and earned/queued disjointness. | A flag or explicit state; final layout TBD | Incorrect durability claims if states conflated | Yes, full ring, queue off, already earned | Burst unlock and menu-exit persistence |
| 5. Export DS badge packs | Viewer API/network/image code plus exporter | Fetch entire set, resize/quantize, write validated v1 pack. Coordinate with active viewer job. | One decoded image and bounded conversion buffers | Partial packs, wrong identity | Yes, PNG fixtures and golden binary files | 3DS export and SD replacement |
| 6. Add paused badge reads | Launcher setup, bootloader handoff, ARM7 `inGameMenu()` command switch and cardengine file handling | Implemented: launch-validated descriptor, paused binary lookup/read, IGM payload CRC, completion/error status and private staging. | One 544-byte IGM buffer and 16-byte descriptor; no external scratch | SD ownership, cache coherency, save/page-file corruption | Yes, parser, bounds, short reads, wrong game | NTR/TWL, fragmented SD, missing file, navigation stress |
| 7. Render badge cards | `arm9_igm/source/inGameMenu.c`, renderer helpers | Implemented: nine rows with selected 32 by 32 preview, locked palette, measured bars and detail badge. | 512 bytes badge VRAM and 512 bytes additional backup; palette already backed up | Any unbacked VRAM write corrupts resumed graphics | Yes, tile conversion and touched-range accounting | Repeated open/close across games and graphics transitions |
| 8. Restore progress/primed events | `arm9_ra/source/ra_rc_frame.c`, header, cadence/diagnostic tests | Match vendored event semantics without changing evaluation cadence. Add bounded candidate capture. | Small candidate; optional cached index mapping | Timing and semantic regression | Yes, compare event traces against vendored runtime | Heavy set and worst gameplay transitions |
| 9. Add throttled text progress popups | `ra_rcheevos.c`, `ra_text.c`, `ra_overlay.c`, configuration | Post-frame scheduling, cooldowns, unlock priority, immutable displayed strip. Keep existing allowlist policy. | Additional frozen strip about 2 KiB plus bounded scheduler state | Gameplay timing and VRAM contention | Yes, ordering, reset, cooldown, simultaneous unlock | Required per allowlisted game |
| 10. Support larger sets and optional gameplay badges | Definitions/index/parser/loader budgets; overlay if needed | Treat 300-achievement evaluation and full metadata as separate capacity work. Reserve cache memory explicitly if pursued. | Measured per set; no safe universal estimate | Highest compatibility and memory risk | Yes, fit and capacity tests | Broad game matrix required |

Stages 2 and 3 deliver useful progress without waiting for badge storage or gameplay event work. Stages 5 through 7 deliver proper icons while paused. Stage 9 should remain text-only initially.

## Corruption risks and current mitigations

**Confirmed-by-code mitigation, not proof of universal safety:**

| Failure class | Current handling |
|---|---|
| Disabled or borrowed BG layer treated as free VRAM | Survey includes every layer’s referenced storage, including disabled layers and the borrowed layer. |
| Wrong footprint for bitmap/affine modes | Mode-aware survey; unsupported sub-engine modes deny allocation. |
| Game reclaims popup storage | Active-frame surveys evict the overlay when the game references its slot/block. |
| Game DMA overwrites popup tiles | Probes across the strip trigger conditional redraw. |
| Rewriting the whole strip every frame consumes VBlank | Upload only when probes find stale data; OAM/palette still receive ongoing maintenance. |
| Sprite overlay alters affine matrices | Sprite path leaves OAM’s fourth halfword untouched. |
| Sprite path needs disabled OBJ hardware | Requires OBJ already enabled and 1D mapping. |
| Unlock overlaps game card reads | ARM9 defers mailbox offers while relevant card reads are pending. |

Evidence: `retail/cardenginei/arm9_ra/source/ra_overlay.c:480`, `:510`, `:990`, `:1009`, `:1240`, `:1327`, `:1344`; `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:649`.

These surveys infer ownership from hardware state. They cannot establish that a game will never stream data into currently unreferenced memory. Source comments contain historical corruption and hang reports, but this investigation did not reproduce them.

The NTR ARM7 path now buffers up to 128 pending IDs and writes them at paused drain points. The WRAM ring also holds 128. The earlier three-entry-buffer description is outdated.

Evidence: `retail/common/include/ra_unlock.h:6`; `retail/cardenginei/arm9_ra/source/ra_rcheevos.c:419`; `retail/cardenginei/arm7/source/ra_unlock.inc:101`, `:122`.

The TWLSDK branch still appends during its service path when no read is ongoing. It needs its own testing; do not assume NTR drain behavior applies.

Evidence: `retail/cardenginei/arm7/source/ra_unlock.inc:91`.

Badge reads must not expand shared slots, interfere with queue handoff, reuse unprotected game RAM, or write arbitrary SD destinations. ARM7 should accept only a prepared file, bounded offsets, and a fixed valid staging range.

## Device acceptance and open questions

Hardware behavior remains unverified for the owner’s New 2DS XL. Existing source comments are historical evidence, not a successful owner-device test.

Required tests:

1. First real unlock with overlay off, then menu earned marker, SD commit, and next-launch acknowledgement.
2. An achievement with measured conditions: compare numerator, target, reset behavior, and percentage against a reference trace.
3. Repeated menu entry/exit before runtime initialization completes and after unlocks.
4. Byte restoration of every additional tile/map/palette region touched by badge cards.
5. Game save/load and streaming transitions before and after menu badge reads.
6. NTR and TWLSDK variants, missing cache, corrupt cache, short read, fragmented file, full SD, and rapid paging.
7. Simultaneous unlocks and progress updates, including fades, display-off, and SD queue refusal.
8. Heavy-set timing, stack high-water marks, allocator margin, overall reader cost, and overrun behavior.
9. Overlay-off and `RA_READER_ENABLED=0` comparisons. DLDI/GSDD variants are excluded by default and must not accidentally acquire unsupported RA dependencies.

Evidence for default variant exclusions: `retail/common/include/ra.h:42`.

Only the owner or device tests can settle:

- Which owned title supplies the first reproducible measured-progress achievement?
- Are 32×32 quantized badges readable and attractive enough on the actual screen?
- Which games can be added to the popup allowlist after transition-heavy testing?
- Is a compact list or four larger badge cards the preferred primary menu view?
- Does measured progress need to survive a reboot? Current live trigger progress should not be treated as a saved achievement state without a separate persistence design.

## Investigation receipts

All checks were static reads and arithmetic. No test suite was executed.

Representative executed command and output excerpts:

```text
rg -n 'static u32 firstId|static.*firstId|RA_TEXT_(COLS|ROWS|WORDS|MARGIN)' retail/cardenginei/arm9_ra/source/ra_rcheevos.c retail/common/include/ra.h

retail/common/include/ra.h:972:#define RA_TEXT_COLS  32
retail/common/include/ra.h:973:#define RA_TEXT_ROWS  2
retail/cardenginei/arm9_ra/source/ra_rcheevos.c:225:static u32 firstId;
```

Executed source reads included:

- `nl -ba retail/cardenginei/arm9_ra/source/ra_rc_frame.c | sed -n '1,255p'`
- `nl -ba retail/cardenginei/arm9_ra/source/ra_rcheevos.c | sed -n '1533,1578p'`
- `nl -ba retail/cardenginei/arm9_ra/rcheevos/src/rcheevos/runtime.c | sed -n '237,285p;535,617p'`
- `nl -ba retail/cardenginei/arm9_igm/source/inGameMenu.c | sed -n '620,990p'`

These establish the custom-loop dispatch, runtime call chain, reference event semantics, and existing menu respectively.

The executed Python inventory and arithmetic check printed:

```text
ELF/MAP inventory:
IGM static arrays 10432
IGM header bytes 3964 aligned 3968
IGM linker budget 39936 paging bytes 40960
image budgets raw including per-image palette:
32x32 4bpp 54400 163200
24x24 4bpp 32000 96000
32x32 8bpp 153600 460800
32x32 RGB555 204800 614400
new doc False
```

A separate executed calculation printed:

```text
text capacity 29431
viewer 0x377f400 session 0x377f300 pending 0x377fe00
cache 100 57664 bytes 56.3125 KiB
cache 300 172864 bytes 168.8125 KiB
design exists False
```

Key source hashes were identical at the beginning and end of the focused investigation:

| File | SHA-256 |
|---|---|
| `retail/cardenginei/arm9_ra/source/ra_rc_frame.c` | `d4a24c7740cdfa22b55ad8f13ff57f0b5ca3d09e4391ff3f0c38be87860a0705` |
| `retail/cardenginei/arm9_ra/source/ra_rcheevos.c` | `493cf207d2bcd4a1d2172f454938e615020f246ba2b883499021e725457178c9` |
| `retail/cardenginei/arm9_igm/source/inGameMenu.c` | `cc3835a6526025ccf98b876cbe07619811bcf2ef9f6e696faf36898814cd49b4` |
| `retail/common/include/locations.h` | `839a980eb5470d81e22f15eb4c92d21711760c0153f723d6dc667416fcead50a` |

Ruled out by inspection:

- Missing vendored measured-progress support.
- Absence of an in-game Achievements page.
- Need for gameplay networking to display progress.
- Need for DS PNG decoding when the viewer exports converted badges.
- Availability of an unused shared-mailbox word.
- Safety of assuming the extended ROM-cache region is free.
- Continued applicability of the old sliced-frame and three-entry-queue descriptions.

Not established: actual IGM binary margin, device timing, whole-set compatibility, current plain-HTTP media service, visual quality, or owner-device unlock correctness.

No temporary artifacts were created, so no cleanup or reclaimed-space claim applies.

