#!/bin/bash
# Build the Galaxy Note 5 stage-1 probe (a Samsung S-Boot "kernel" Image).
#
#   build.sh                                  -> out/Image
#   build.sh STOCK_BOOT.img OUT_BOOT.img      -> also packs a BOOT image
#
# Uses clang + ld.lld (any recent LLVM) so no GNU aarch64 cross-gcc is needed.
# Override with CC=aarch64-linux-gnu-gcc if you prefer GCC.
set -euo pipefail
cd "$(dirname "$0")"
CC="${CC:-clang --target=aarch64-linux-gnu}"
LD="${LD:-ld.lld}"
OBJCOPY="${OBJCOPY:-llvm-objcopy}"
READELF="${READELF:-llvm-readelf}"
CFLAGS="-O2 -ffreestanding -nostdlib -fno-builtin -fno-pic -fno-jump-tables -mgeneral-regs-only -mstrict-align -fno-stack-protector -march=armv8-a -Wall -Wextra -Werror"

mkdir -p out
$CC $CFLAGS -c start.S -o out/start.o
$CC $CFLAGS -c loader.c -o out/loader.o
$CC $CFLAGS -c fdt.c -o out/fdt.o
$CC $CFLAGS -c discover.c -o out/discover.o

# The image runs wherever S-Boot puts it and nothing relocates it. start.o is
# excluded on purpose: its one ABS64 is the header's image_size (.quad _end),
# which is meant to be the absolute link-time value (linked at 0).
abs=$($READELF -r out/loader.o out/fdt.o out/discover.o | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations (not position independent)" >&2; exit 1; }

$LD -T loader.ld -static -nostdlib --build-id=none -N out/start.o out/loader.o out/fdt.o out/discover.o -o out/loader.elf
$OBJCOPY -O binary out/loader.elf out/Image
end_hex=$($READELF -s out/loader.elf | awk '$8=="_end"{print $2}')
if [ -n "$end_hex" ]; then
	size=$((16#$end_hex))
	if [ "$size" -gt "$(stat -c %s out/Image)" ]; then
		truncate -s "$size" out/Image	# pad to the header's image_size
	fi
fi
echo "out/Image: $(stat -c %s out/Image) bytes"

if [ $# = 2 ]; then
	: > out/empty.ramdisk
	python3 ../../tools/mkboot_samsung.py "$1" out/Image out/empty.ramdisk "$2"
	sha256sum "$2"
fi
