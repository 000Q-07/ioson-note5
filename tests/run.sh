#!/bin/bash
# Host test for the probe's device-tree reader. Needs python3 and clang (or set CC).
# AddressSanitizer + UBSan are used when the compiler has them.
set -euo pipefail
cd "$(dirname "$0")"
CC="${CC:-clang}"
B=out
mkdir -p $B
python3 make_fake_dtb.py $B
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
if ! echo 'int main(void){return 0;}' | $CC -x c - $SAN -o $B/san_probe 2>/dev/null; then
	echo "note: sanitizers unavailable, building without them"
	SAN=""
fi
$CC -g -O1 -Wall -Wextra -Werror $SAN -I$B \
	test_discover.c ../shim/noble/fdt.c ../shim/noble/discover.c -o $B/test_discover
$B/test_discover $B "${1:-100000}"
