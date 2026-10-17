#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
CC=${CC:-cc}
RA_HOST_OUT=$(mktemp -d "${TMPDIR:-/tmp}/ra-host.XXXXXX")
trap 'rm -rf "$RA_HOST_OUT"' EXIT HUP INT TERM
RC=retail/cardenginei/arm9_ra/rcheevos
if [ ! -f "$RC/include/rc_runtime.h" ]; then
    echo "$RC is empty; initialize the pinned submodule" >&2
    exit 2
fi

set --
for source in "$RC"/src/rcheevos/*.c; do
    case "$source" in */rc_validate.c) continue ;; esac
    set -- "$@" "$source"
done
"$CC" -std=gnu99 -Wall -Wextra -O1 \
    -Itools/host-stubs -Iretail/common/include -Iretail/cardenginei/arm9_ra/include \
    -I"$RC/include" -I"$RC/src" \
    tools/ra_cadence_test.c retail/cardenginei/arm9_ra/source/ra_rc_frame.c \
    "$@" "$RC/src/rc_util.c" "$RC/src/rc_compat.c" "$RC/src/rc_version.c" \
    "$RC/src/rhash/md5.c" -lm -pthread -o "$RA_HOST_OUT/cadence"
"$RA_HOST_OUT/cadence"

"$CC" -std=gnu99 -Wall -Wextra -Wno-unused-function \
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -O1 \
    -Itools/host-stubs -Iretail/common/include -Iretail/cardenginei/arm9_ra/include \
    -I"$RC/include" -I"$RC/src" \
    tools/ra_diagnostics_test.c retail/cardenginei/arm9_ra/source/ra_rc_frame.c \
    "$@" "$RC/src/rc_util.c" "$RC/src/rc_compat.c" "$RC/src/rc_version.c" \
    "$RC/src/rhash/md5.c" -lm -pthread -o "$RA_HOST_OUT/diagnostics"
"$RA_HOST_OUT/diagnostics"

"$CC" -std=gnu99 -Wall -O1 \
    -DRA_LAUNCHER_WIFI=1 -DRC_HASH_NO_DISC -DRC_HASH_NO_ENCRYPTED -DRC_HASH_NO_ZIP \
    -Dsniprintf=snprintf \
    -Itools/host-stubs -Iretail/common/include -Iretail/cardenginei/arm9_ra/include \
    -I"$RC/include" -I"$RC/src" \
    tools/ra_launcher_test.c \
    retail/arm9/source/ra_hash.c retail/arm9/source/ra_cfg.c retail/arm9/source/ra_screen.c \
    retail/arm9/source/ra_patch.c retail/arm9/source/ra_queue.c \
    "$RC/src/rhash/md5.c" "$RC/src/rhash/hash.c" "$RC/src/rhash/hash_rom.c" \
    "$RC/src/rc_util.c" "$RC/src/rc_compat.c" \
    -pthread -o "$RA_HOST_OUT/launcher"
if "$RA_HOST_OUT/launcher" > "$RA_HOST_OUT/launcher.log" 2>&1; then
    tail -n 2 "$RA_HOST_OUT/launcher.log"
    awk '$1 == "ok" { checks++ } END { printf "launcher: PASSED (%d checks)\n", checks }' "$RA_HOST_OUT/launcher.log"
else
    cat "$RA_HOST_OUT/launcher.log"
    exit 1
fi
