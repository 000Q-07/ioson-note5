#!/usr/bin/env python3
"""Suggest shim/noble/board.h values from a tools/collect-facts.sh folder.

Usage: make_board_h.py facts/note5-YYYYmmdd-HHMMSS

It only PRINTS what it finds; nothing is written. Read the suggestions, check
them against the raw files in the facts folder, then edit shim/noble/board.h
yourself. Anything it can't find is reported as "not found" - do not guess.
"""
import os
import re
import sys

if len(sys.argv) != 2 or not os.path.isdir(sys.argv[1]):
    sys.exit(__doc__)
d = sys.argv[1]


def read(name):
    try:
        with open(os.path.join(d, name), errors='replace') as f:
            return f.read()
    except OSError:
        return ''


print('--- device ---')
print(read('model.txt').strip() or 'model.txt missing')

print('\n--- RAM and reserved regions (/proc/iomem) ---')
iomem = read('iomem.txt')
ram = []
for m in re.finditer(r'^\s*([0-9a-f]{8,})-([0-9a-f]{8,}) : (.+)$', iomem, re.M):
    lo, hi, name = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
    if name.strip() == 'System RAM':
        ram.append((lo, hi))
        print(f'  RAM       {lo:#x}-{hi:#x}  ({(hi - lo + 1) >> 20} MiB)')
    elif re.search(r'ramoops|pstore|console|framebuffer|fb|reserved', name, re.I):
        print(f'  {name.strip():<12}{lo:#x}-{hi:#x}  ({(hi - lo + 1) >> 10} KiB)')
if not ram:
    print('  no "System RAM" lines found (is /proc/iomem restricted?)')

print('\n--- framebuffer hints (cmdline, dmesg) ---')
found = False
for src in ('cmdline.txt', 'dt-chosen.txt', 'dmesg-hw.txt'):
    for m in re.finditer(r'(\S*(?:bootloaderfb|framebuffer|lcd|fb_base|decon)\S*)', read(src), re.I):
        print(f'  [{src}] {m.group(1)}')
        found = True
if not found:
    print('  not found - the S8 had s3cfb.bootloaderfb=0x... in the kernel cmdline;'
          ' the Note 5 may use a different name. Search the raw files for 0x-addresses'
          ' next to "fb" or "decon".')

print('\n--- ramoops (pstore) ---')
ro = re.findall(r'ramoops[^\n]*', read('dmesg-hw.txt'), re.I) + re.findall(r'[^\n]*ramoops[^\n]*', iomem, re.I)
print('\n'.join('  ' + x.strip() for x in ro[:10]) or '  not found - see pstore.txt / dt-ramoops.txt')

print('\n--- partitions that look like BOOT ---')
for line in read('by-name.txt').splitlines():
    if re.search(r'\bboot\b|recovery|userdata|efs', line, re.I):
        print('  ' + line.strip())

print('\n--- stock boot image ---')
print(read('boot-info.txt').strip() or 'no boot-info.txt (BOOT copy failed?)')

print('\nNext: put confirmed values in shim/noble/board.h. Unknown = leave 0.')
