# ioson-note5

Experimental groundwork for booting Apple's open-source XNU kernel (and,
eventually, an iPhone X iOS 16.7 userland) on the **Samsung Galaxy Note 5
(SM-N920C / SM-N920I, Exynos 7420)**.

> **Status: stage 0 + stage 1 only. Nothing here boots iOS.** No Note 5 port
> of this kind is known to exist; this repo is the start of one. Expect a long
> series of flash-look-at-the-log-repeat cycles on a real phone.

It is modelled on the Galaxy S8 port
[Floppikrob/ios-galaxy-s8](https://github.com/Floppikrob/ios-galaxy-s8), which
is the work that proved the approach on a Samsung Exynos phone. The loader
here is adapted from that project. All board addresses are **unknown** for the
Note 5 and must be measured; none of the S8's numbers are reused.

## Risks

- Flashing writes to your phone's BOOT partition. A wrong image can leave the
  phone unable to start Android until you restore BOOT from TWRP or Odin.
  Android itself is not touched until you flash.
- The bootloader must be unlocked and TWRP installed (this trips Knox and may
  void warranty). Carrier models (e.g. SM-N920T/P/V/A) may be locked.
- Make a **full TWRP backup, including EFS**, before flashing anything.
- This project does not write to USERDATA, EFS, recovery or the bootloaders.

## What is here

| Path | What it does |
| --- | --- |
| `tools/collect-facts.sh` | **Stage 0.** Read-only: collects RAM map, partitions, kernel cmdline, device tree and a copy of the stock BOOT image from the phone in TWRP. |
| `tools/make_board_h.py` | Prints board.h suggestions from the collected facts (never guesses). |
| `shim/noble/` | **Stage 1.** Minimal probe: S-Boot runs it from BOOT; it prints EL, MIDR, DTB pointer etc. to the screen and a pstore log, then parks. Boots nothing. |
| `tools/mkboot_samsung.py`, `tools/bootimg_info.py` | Pack / inspect Samsung boot images (S8 layout, **unverified** on the Note 5). |

## How to use it

### Stage 0: collect facts (safe, read-only)

1. Phone in TWRP, USB cable connected, `adb devices` shows it.
2. `bash tools/collect-facts.sh`
3. `python3 tools/make_board_h.py facts/note5-<timestamp>`
4. Look at the raw files, then fill in `shim/noble/board.h`. Unknown stays `0`.

Send the contents of `model.txt`, `iomem.txt`, `cmdline.txt`, `by-name.txt`,
`boot-info.txt` (not the `.img`) to whoever is helping; those hold no personal
data.

### Stage 1: does S-Boot run our code?

```bash
bash shim/noble/build.sh                         # -> shim/noble/out/Image
bash shim/noble/build.sh facts/.../boot.img note5-probe-boot.img   # packs a BOOT image
```

Needs `clang`, `ld.lld`, `llvm-objcopy`, `llvm-readelf`, `python3`. Check
`boot-info.txt` first: if the stock BOOT image does not look like the S8's
layout, **do not flash** the packed image; the packer needs adapting.

Flash only with a TWRP backup in hand, from TWRP (`dd` to the BOOT node that
`collect-facts.sh` reported in `boot-device.txt`). Read the log after a
reboot into TWRP: `adb shell cat /sys/fs/pstore/console-ramoops-0`.

## Known unknowns (the actual work)

1. S-Boot's entry state and where it loads the image (stage 1 reports this).
2. Framebuffer address and whether the AMOLED needs a DECON trigger unmask.
3. ramoops address (the log channel that survives reboots).
4. Exynos 7420 SMP bring-up (4x A57 + 4x A53), GIC, timer.
5. UFS 2.0 host controller driver, partition layout.
6. Device tree for XNU, then the iOS root image (S8 steps in its docs).

## Licensing and provenance

`shim/font.h`, `shim/noble/*`, `tools/mkboot_samsung.py` and
`tools/bootimg_info.py` are derived from
[Floppikrob/ios-galaxy-s8](https://github.com/Floppikrob/ios-galaxy-s8) (itself
built on orhunnx/ios-a5xelte and dipa-studio/ios-sweet). Files carry the SPDX
headers they had there; the upstream projects state no overall license for the
rest, so check with them before redistributing publicly. iOS and Apple firmware
are not included and must never be committed (see `.gitignore`).
