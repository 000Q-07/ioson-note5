#!/usr/bin/env python3
"""Build a Samsung (Exynos) boot.img v0 that reuses the stock header and DT.

Adapted from tools/dreamlte_mkboot.py in Floppikrob/ios-galaxy-s8 (Galaxy S8).
Takes addresses, page size, os_version, name, cmdline and the 'dt' section
from the stock boot.img; replaces kernel and ramdisk. Appends
SEANDROIDENFORCE like the stock image. The S8 layout is NOT verified for the
Note 5: compare with tools/bootimg_info.py output first.
Usage: mkboot_samsung.py stock_boot.img Image ramdisk out.img
"""
import hashlib
import struct
import sys

stock = open(sys.argv[1], 'rb').read()
kernel = open(sys.argv[2], 'rb').read()
ramdisk = open(sys.argv[3], 'rb').read()
assert stock[:8] == b'ANDROID!'
(ksz, kaddr, rsz, raddr, ssz, saddr, tags, page, dtsz, osver) = struct.unpack_from('<10I', stock, 8)


def pages(n):
    return (n + page - 1) // page * page


off = page + pages(ksz) + pages(rsz) + pages(ssz)
dt = stock[off:off + dtsz]
if dt[:4] != b'DTBH':
    # Layout verified on the Galaxy S8 only. Run tools/bootimg_info.py on the
    # Note 5's stock BOOT image and check the dt section before flashing.
    print(f'WARNING: stock dt section starts with {dt[:4]!r}, not b"DTBH"; '
          'check tools/bootimg_info.py output before flashing', file=sys.stderr)

sha = hashlib.sha1()
for blob in (kernel, ramdisk, b'', dt):
    sha.update(blob)
    sha.update(struct.pack('<I', len(blob)))
digest = sha.digest() + b'\0' * 12

hdr = bytearray(stock[:page])
struct.pack_into('<10I', hdr, 8, len(kernel), kaddr, len(ramdisk), raddr, 0, saddr, tags, page, len(dt), osver)
hdr[0x240:0x260] = digest                 # stock cmdline fields are kept as they are

out = bytearray(hdr)
for blob in (kernel, ramdisk, dt):
    out += blob + b'\0' * (pages(len(blob)) - len(blob))
out += b'SEANDROIDENFORCE'
open(sys.argv[4], 'wb').write(out)
print(f'{sys.argv[4]}: {len(out)} bytes (kernel {len(kernel)}, ramdisk {len(ramdisk)}, dt {len(dt)})')
