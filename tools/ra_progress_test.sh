#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
CC=${CC:-cc}
RA_PROGRESS_OUT=$(mktemp -d "${TMPDIR:-/tmp}/ra-progress.XXXXXX")
trap 'rm -rf "$RA_PROGRESS_OUT"' EXIT HUP INT TERM
RC=retail/cardenginei/arm9_ra/rcheevos
set --
for source in "$RC"/src/rcheevos/*.c; do
    case "$source" in */rc_validate.c) continue ;; esac
    set -- "$@" "$source"
done
"$CC" -std=gnu99 -Wall -Wextra -Wno-unused-function \
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -O1 \
    -Itools/host-stubs -Iretail/common/include -Iretail/cardenginei/arm9_ra/include \
    -I"$RC/include" -I"$RC/src" \
    tools/ra_progress_test.c retail/cardenginei/arm9_ra/source/ra_rc_frame.c \
    "$@" "$RC/src/rc_util.c" "$RC/src/rc_compat.c" "$RC/src/rc_version.c" \
    "$RC/src/rhash/md5.c" -lm -pthread -o "$RA_PROGRESS_OUT/progress"
"$RA_PROGRESS_OUT/progress"

if command -v arm-none-eabi-nm >/dev/null 2>&1 \
    && [ -f retail/cardenginei/arm9_ra/build/cardenginei_arm9_ra.elf ] \
    && [ -f retail/cardenginei/arm9_igm/build/cardenginei_arm9_igm.elf ]; then
python3 - <<'PYLAYOUT'
import pathlib, re, struct, subprocess

def symbols(path):
    result = {}
    for line in subprocess.check_output(['arm-none-eabi-nm', path], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            result[fields[2]] = (int(fields[0], 16), fields[1])
    return result

ra = symbols('retail/cardenginei/arm9_ra/build/cardenginei_arm9_ra.elf')
words = struct.unpack('<9I', pathlib.Path('retail/nitrofiles/cardenginei_arm9_ra.bin').read_bytes()[:36])
assert words[1] == 0 and words[3] == 0x314f4152
assert words[5:] == (0x31514152, 1, 1, 16)
for offset, name in [(0, 'ra_wram_tick'), (8, 'ra_wram_offer'), (16, 'ra_rc_query_progress')]:
    branch = words[offset // 4]
    assert branch >> 24 == 0xea
    delta = branch & 0xffffff
    if delta & 0x800000:
        delta -= 0x1000000
    assert 0x03740000 + offset + 8 + 4 * delta == ra[name][0], name
assert ra['queryGuard'][1].upper() == 'D'
igm = symbols('retail/cardenginei/arm9_igm/build/cardenginei_arm9_igm.elf')
header = pathlib.Path('retail/common/include/igm_text.h').read_text()
size = int(re.search(r'sizeof\(IgmText\) == (0x[0-9A-Fa-f]+)', header)[1], 16)
assert igm['sharedAddr'][0] - igm['igmText'][0] == (size + 15) & ~15
assert igm['__bss_end'][0] <= 0x02f88000 + 39936
assert len(pathlib.Path('retail/nitrofiles/cardenginei_arm9_ra.bin').read_bytes()) <= 131072
print('binary ABI: old offsets 0/4/8/12, query branch at 16, RAQ1/version/capability/size, loaded guard, IGM header 3968 and linker limits passed')
PYLAYOUT
fi
