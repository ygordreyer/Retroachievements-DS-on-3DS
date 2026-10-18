#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
RA_BADGE_OUT=$(mktemp -d "${TMPDIR:-/tmp}/ra-badge.XXXXXX")
trap 'rm -rf "$RA_BADGE_OUT"' EXIT HUP INT TERM
python3 - "$RA_BADGE_OUT" <<'PY'
from pathlib import Path
import sys

def function(path, signature):
    source = Path(path).read_text()
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

out = Path(sys.argv[1])
out.joinpath('ra_badge_file_read.h').write_text(function(
    'retail/common/source/my_fat.c', 'u32 fileRead ('))
out.joinpath('ra_badge_layout.h').write_text(''.join(function(
    'retail/cardenginei/arm9_igm/source/inGameMenu.c', signature)
    for signature in ('static void drawCursor(', 'static void raDrawProgress(')))
PY
"${CC:-cc}" -std=gnu17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Iretail/common/include -I"$RA_BADGE_OUT" tools/ra_badge_test.c -o "$RA_BADGE_OUT/badge"
"$RA_BADGE_OUT/badge"
for variant in single two; do
    set --
    if [ "$variant" = two ]; then set -- -DTWOCARD; fi
    "${CC:-cc}" -std=gnu17 -Wall -Wextra -Werror -Wno-sign-compare -Wno-misleading-indentation \
        -fsanitize=address,undefined "$@" -I"$RA_BADGE_OUT" \
        tools/ra_badge_fat_test.c -o "$RA_BADGE_OUT/fat-$variant"
    "$RA_BADGE_OUT/fat-$variant"
done
python3 - <<'PY'
from pathlib import Path
import hashlib
import shutil
import subprocess
import re
s = Path('retail/cardenginei/arm9_igm/source/inGameMenu.c').read_text()
assert 'static u8 bgBak[RA_BADGE_BACKUP_BYTES]' in s
assert 'tonccpy(bgBak, BG_GFX_SUB, sizeof(bgBak))' in s
assert 'tonccpy(BG_GFX_SUB, bgBak, sizeof(bgBak))' in s
assert 'static u16 bgMapBak[0x300]' in s and 'static u16 palBak[256]' in s
assert 'tonccpy(BG_MAP_RAM_SUB(15), bgMapBak, sizeof(bgMapBak))' in s
assert 'tonccpy(BG_PALETTE_SUB, palBak, sizeof(palBak))' in s
assert 'raMenuBadge(e->id' in s and ', 27, 3 + 2 * cursor);' in s and ', 27, 19);' in s
assert 'text + e->titleOff, 18,' in s and 'printRight(26, y,' in s
assert 'DC_FlushRange(raBadgeImage, sizeof(raBadgeImage))' in s
assert 'DC_InvalidateRange(raBadgeImage, sizeof(raBadgeImage))' in s
assert hashlib.sha256(Path('tools/fixtures/badges-v1.rab').read_bytes()).hexdigest() == '5de24c53a426cdbc20e59e34bfa28a328e2cde41d3a75075e6128b25efa72d28'
print('badge integration: production backup/restore, map positions, cache coherence and golden SHA-256 passed')

boot = Path('retail/bootloaderi/source/arm7/main.arm7.c').read_text()
hook = Path('retail/bootloaderi/source/arm7/hook_arm7.c').read_text()
capture = boot.index('raBadgeDescriptor raBadgePack = {0};')
clear = boot.index('*(u32*)CARDENGINEI_ARM9_RA_SESSION_BUFFERED_LOCATION = 0;')
calls = list(re.finditer(r'errorCode = hookNdsRetailArm7\((.*?)\n\s*\);', boot, re.S))
assert len(calls) == 2 and capture < calls[0].start() < clear < calls[1].start()
assert all('&raBadgePack,' in call[1] for call in calls)
assert re.search(r'if \(\*\(u32\*\)CARDENGINEI_ARM9_RA_SESSION_BUFFERED_LOCATION == CARDENGINEI_ARM9_RA_SESSION_MAGIC\)\s+raBadgePack =', boot[capture:])
assert 'CARDENGINEI_ARM9_RA_SESSION_BUFFERED_LOCATION' not in hook
assert 'toncset(&ce7->raBadgePack, 0, sizeof(ce7->raBadgePack));' in hook
assert 'if (dsiSD && raBadgePack->magic == RA_BADGE_DESCRIPTOR_MAGIC)' in hook
assert 'ce7->raBadgePack = *raBadgePack;' in hook
print('badge handoff: capture before session clear, DSiWare and retail hook arguments, absent-magic zero and SD/descriptor guards passed')

if shutil.which('arm-none-eabi-nm'):
    def symbols(path):
        result = {}
        for line in subprocess.check_output(['arm-none-eabi-nm', '-S', str(path)], text=True).splitlines():
            fields = line.split()
            if len(fields) == 4:
                result[fields[3]] = (int(fields[0], 16), int(fields[1], 16))
            elif len(fields) == 3:
                result[fields[2]] = (int(fields[0], 16), 0)
        return result
    for name in ('arm7', 'arm7_alt', 'arm7_twlsdk', 'arm7_twlsdk3'):
        elf = Path(f'retail/cardenginei/{name}/build/cardenginei_{name}.elf')
        if not elf.exists():
            continue
        sym = symbols(elf)
        start, end, ceiling = sym['ce7'][0], sym['__bss_end'][0], sym['__vram_top'][0]
        assert sym['raBadgePack'][0] - start == 0xf0
        assert end <= ceiling
        print(f'{name}: allocated {end-start}/{ceiling-start} bytes, remaining {ceiling-end}, badge descriptor offset 0xf0 passed')
    elf = Path('retail/cardenginei/arm9_igm/build/cardenginei_arm9_igm.elf')
    if elf.exists():
        sym = symbols(elf)
        image, size = sym['raBadgeImage']
        start, end, ceiling = sym['igmText'][0], sym['__bss_end'][0], sym['__vram_top'][0]
        assert size == 544 and image % 32 == 0
        assert start + 0x1000 <= image and image + size <= end <= ceiling
        assert sym['bgBak'][1] == 0x2200
        assert sym['bgMapBak'][1] == 1536 and sym['palBak'][1] == 512
        print(f'IGM: allocated {end-start}/{ceiling-start} bytes, remaining {ceiling-end}, aligned private staging and backed-up ranges passed')
PY
