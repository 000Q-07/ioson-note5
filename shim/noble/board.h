/* SPDX-License-Identifier: GPL-2.0-only
 * Galaxy Note 5 (SM-N920C / SM-N920I, Exynos 7420, "noblelte") board facts.
 *
 * EVERY value here is UNKNOWN until measured on the phone. 0 means "unknown,
 * feature disabled". Fill them from the files produced by tools/collect-facts.sh
 * (tools/make_board_h.py prints suggestions). Do not copy S8 numbers: they are
 * for a different SoC.
 */
#pragma once

/* S-Boot framebuffer (look for a "bootloaderfb"-style address in the cmdline
 * or dmesg, or a framebuffer entry in /proc/iomem). The Note 5 panel is
 * 1440x2560; the stride below assumes 4 bytes per pixel and no padding. */
#define FB_BASE			0x0UL
#define FB_W			1440
#define FB_H			2560
#define FB_STRIDE		(FB_W * 4)

/* Command-mode panels need their transfer trigger unmasked before writes reach
 * the screen (the S8 needed this). Unknown for the Note 5: leave 0 first; if
 * ramoops works but the screen stays black, this is the first suspect. */
#define DECON_TRIG_ADDR		0x0UL
#define DECON_TRIG_VAL		0x0U

/* Persistent RAM log (pstore/ramoops). Take the console zone address from
 * /proc/iomem or the TWRP kernel's DT/dmesg. Assumes ecc 0 and the standard
 * header (sig "DBGC", start, size) as on the S8. */
#define RAMOOPS_CONSOLE		0x0UL
#define RAMOOPS_CONSOLE_SIZE	0x0UL
