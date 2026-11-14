#!/usr/bin/env bash
# Read-only hardware fact collector for the Galaxy Note 5 (Exynos 7420,
# SM-N920C / SM-N920I). Run it on your PC with the phone in TWRP and USB
# debugging (adb) working:
#
#     bash tools/collect-facts.sh            # writes facts/note5-<timestamp>/
#
# It only READS from the phone. The one "copy" it makes is a read-only dd of
# the BOOT partition into TWRP's RAM disk (/tmp), pulled to your PC and then
# deleted from the phone. Nothing is flashed or modified.
#
# Keep the output folder private: it contains a copy of Samsung's boot image
# (the facts/ folder is in .gitignore for that reason).
set -u

OUT="${1:-facts/note5-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"

if ! adb get-state >/dev/null 2>&1; then
	echo "No device found by adb. Boot the phone into TWRP, plug it in, and retry." >&2
	exit 1
fi

run() {	# run NAME 'shell command on the phone'
	local name="$1"
	shift
	echo "== $name"
	adb shell "$@" 2>&1 | tr -d '\r' > "$OUT/$name.txt"
}

run model 'for p in ro.product.model ro.product.device ro.boot.bootloader ro.bootloader ro.boot.flash.locked ro.boot.warranty_bit ro.boot.em.status ro.boot.verifiedbootstate; do echo "$p=$(getprop $p)"; done'
run cpuinfo 'cat /proc/cpuinfo'
run iomem 'cat /proc/iomem'
run cmdline 'cat /proc/cmdline'
run meminfo 'head -3 /proc/meminfo'
run partitions 'cat /proc/partitions'
run by-name 'ls -l /dev/block/by-name /dev/block/platform/*/by-name 2>&1'
run pstore 'ls -l /sys/fs/pstore 2>&1; mount | grep -i pstore'
run dmesg-hw 'dmesg | grep -i -E "ramoops|pstore|reserved|framebuffer|bootloaderfb|decon|ufs|dwmmc|mmc0"'
run dt-chosen 'cat /proc/device-tree/chosen/bootargs 2>&1; echo; ls /proc/device-tree/chosen 2>&1'
run dt-reserved 'ls -R /proc/device-tree/reserved-memory 2>&1 | head -50'
run dt-ramoops "find /proc/device-tree -iname '*ramoops*' 2>/dev/null | head"

echo "== fdt (live device tree)"
adb pull /sys/firmware/fdt "$OUT/fdt.dtb" >/dev/null 2>&1 || echo "no /sys/firmware/fdt on this recovery"
if [ -s "$OUT/fdt.dtb" ] && command -v dtc >/dev/null 2>&1; then
	dtc -I dtb -O dts "$OUT/fdt.dtb" -o "$OUT/fdt.dts" 2>/dev/null
fi

echo "== BOOT partition (read-only copy)"
BOOTDEV="$(adb shell 'for p in /dev/block/by-name/BOOT /dev/block/by-name/boot /dev/block/platform/*/by-name/BOOT /dev/block/platform/*/by-name/boot; do [ -e "$p" ] && echo "$p" && break; done' | tr -d '\r')"
if [ -n "$BOOTDEV" ]; then
	echo "BOOT device node: $BOOTDEV" | tee "$OUT/boot-device.txt"
	adb shell "dd if=$BOOTDEV of=/tmp/note5-boot.img bs=1048576 2>/dev/null; ls -l /tmp/note5-boot.img; sha1sum /tmp/note5-boot.img" | tr -d '\r' >> "$OUT/boot-device.txt"
	adb pull /tmp/note5-boot.img "$OUT/boot.img" >/dev/null 2>&1
	adb shell 'rm -f /tmp/note5-boot.img'
	if [ -s "$OUT/boot.img" ]; then
		python3 "$(dirname "$0")/bootimg_info.py" "$OUT/boot.img" > "$OUT/boot-info.txt" 2>&1 || true
	fi
else
	echo "Could not find the BOOT partition node; see by-name.txt" | tee "$OUT/boot-device.txt"
fi

echo
echo "Done. Facts are in: $OUT"
echo "Next:  python3 tools/make_board_h.py $OUT"
