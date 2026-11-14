#!/usr/bin/env python3
"""Switch an S8-patched XNU tree to the Galaxy Note 5 (SM-N920C / SM-N920I,
Exynos 7420, "noblelte") board-fact block.

Design (copied from tools/dreamlte/apply-dreamlte.py in
Floppikrob/ios-galaxy-s8): the A5XELTE machine config stays; what differs per
phone is a block of A5_* macros in pexpert/pexpert/arm64/A5XELTE.h. This
script runs AFTER the S8 patch series up to and including
0124-dreamlte-s8.patch and replaces the selection of the S8 block with a
NOBLELTE block whose values come from xnu/noble/board.json.

Every value in board.json is something that must be MEASURED on the phone
(tools/collect-facts.sh, then the stage-1 probe in shim/noble). The script
refuses to run while any required value is still null: a made-up framebuffer
or log address would only produce a boot that fails in a confusing way.

What this does NOT do (see docs/xnu-port-worklist.md): it contains no Note 5
driver. With NOBLELTE defined, every other "#if defined(DREAMLTE)" site in the
tree is off, so those files fall back to the Galaxy A5 (Exynos 7580) code
paths. Those are wrong for the 7420 and have to be replaced one by one.

Usage: apply-noble.py XNU_TREE [board.json]
"""
import json
import sys
from pathlib import Path

HEADER = 'pexpert/pexpert/arm64/A5XELTE.h'
REQUIRED = ['fb_phys', 'fb_width', 'fb_height', 'fb_stride_bytes',
            'entry_marker_phys', 'boot_mpidr', 'klog_phys', 'klog_size',
            'use_phys_timer']


def to_int(name, v):
    if isinstance(v, bool):
        return int(v)
    if isinstance(v, int):
        return v
    if isinstance(v, str):
        try:
            return int(v, 0)
        except ValueError:
            pass
    raise SystemExit(f'board.json: {name} = {v!r} is not an integer')


def load_facts(path):
    raw = json.loads(Path(path).read_text())
    missing = [k for k in REQUIRED if raw.get(k) is None]
    if missing:
        raise SystemExit(
            'board.json still has unknown (null) values: ' + ', '.join(missing) +
            '\nMeasure them on the phone first (tools/collect-facts.sh and the shim/noble probe).')
    f = {k: to_int(k, raw[k]) for k in REQUIRED}
    if f['fb_width'] * 4 > f['fb_stride_bytes']:
        raise SystemExit('board.json: fb_stride_bytes is smaller than width * 4 bytes')
    if f['fb_phys'] & 0x1fffff:
        # the early assembly maps the framebuffer in 2 MiB blocks from fb_phys
        raise SystemExit('board.json: fb_phys must be 2 MiB aligned for the early block mapping '
                         f'(got {f["fb_phys"]:#x}); check the value or extend start.s')
    return f


def noble_block(f):
    blocks = -(-(f['fb_height'] * f['fb_stride_bytes']) // (2 << 20))   # ceil to 2 MiB
    lines = [
        '#define NOBLELTE                      1',
        '#if defined(NOBLELTE)',
        '/* Samsung Galaxy Note 5 (SM-N920C/I, Exynos 7420). Values measured on the phone;',
        ' * see xnu/noble/board.json and docs/xnu-port-worklist.md. */',
        f'#define A5_FB_PHYS                    {f["fb_phys"]:#x}',
        f'#define A5_FB_WIDTH                   {f["fb_width"]}',
        f'#define A5_FB_HEIGHT                  {f["fb_height"]}',
        f'#define A5_FB_STRIDE                  {f["fb_stride_bytes"]}',
        f'#define A5_FB_2M_BLOCKS               {blocks}',
        f'#define A5_ENTRY_MARKER_PHYS          {f["entry_marker_phys"]:#x}',
        f'#define A5_BOOT_MPIDR                 {f["boot_mpidr"]:#x}',
        '/* The A5 key/touch code pokes Exynos 7580 registers: off until a 7420 driver exists. */',
        '#define A5_KEYS_DEFAULT               0',
        '#define A5_TOUCH_DEFAULT              0',
        f'#define A5_KLOG_PHYS                  {f["klog_phys"]:#x}',
        f'#define A5_KLOG_SIZE                  {f["klog_size"]:#x}',
    ]
    if f['use_phys_timer']:
        lines += [
            '#define A5_USE_PHYS_TIMER             1',
            '#undef A5XELTE_VTIMER_INTID',
            '#define A5XELTE_VTIMER_INTID         30   /* non-secure physical timer, PPI 14 */',
        ]
    lines.append('#elif defined(DREAMLTE)')
    return '\n'.join(lines) + '\n'


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    tree = Path(sys.argv[1])
    facts_path = sys.argv[2] if len(sys.argv) == 3 else Path(__file__).with_name('board.json')
    f = load_facts(facts_path)

    path = tree / HEADER
    if not path.is_file():
        raise SystemExit(f'{HEADER} not found in {tree} (apply the S8 series through 0124 first)')
    text = path.read_text()
    if 'NOBLELTE' in text:
        raise SystemExit(f'{HEADER}: already contains NOBLELTE (script already applied?)')

    old = '#define DREAMLTE                      1\n#if defined(DREAMLTE)\n'
    found = text.count(old)
    if found != 1:
        raise SystemExit(f'{HEADER}: expected 1x S8 selection block, found {found}. '
                         'Is patch 0124-dreamlte-s8.patch applied?')
    path.write_text(text.replace(old, noble_block(f)))
    print(f'edited {HEADER} (NOBLELTE, fb 2M blocks from board.json)')


if __name__ == '__main__':
    main()
