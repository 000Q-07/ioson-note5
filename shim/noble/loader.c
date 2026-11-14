/* SPDX-License-Identifier: GPL-2.0-only
 * Galaxy Note 5 (SM-N920C / SM-N920I, Exynos 7420) stage-1 probe.
 *
 * Adapted from the Galaxy S8 loader (shim/dreamlte/loader.c in
 * Floppikrob/ios-galaxy-s8). This stage boots nothing. Its only job is to prove
 * that S-Boot runs our code from the BOOT partition and to report what the
 * CPU and S-Boot hand us, on the screen and in a ramoops log that survives a
 * reboot into TWRP:
 *
 *     adb shell cat /sys/fs/pstore/console-ramoops-0
 *
 * Both outputs are optional and controlled by board.h (0 = unknown = off).
 */
#include <stdint.h>
#include "board.h"
#include "../font.h"

/* ---- persistent log (pstore ramoops console zone) ---- */
struct prz {
	uint32_t sig, start, size;
	uint8_t data[];
};
#define PRZ_SIG	0x43474244U	/* "DBGC" */
static volatile struct prz *prz;
static uint32_t log_len;

static void log_ch(char c)
{
	if (!RAMOOPS_CONSOLE || log_len >= RAMOOPS_CONSOLE_SIZE - 12)
		return;
	prz->data[log_len++] = (uint8_t)c;
	prz->start = log_len;
	prz->size = log_len;
}

/* ---- screen console ---- */
#define SCALE	3
#define CELL_W	(8 * SCALE)
#define CELL_H	(16 * SCALE + 6)
#define COLS	((FB_W - 48) / CELL_W)
#define ROWS	((FB_H - 160) / CELL_H)
static int row, col;
static uint32_t fg = 0xffffffffU;

static void fill(int x0, int y0, int w, int h, uint32_t c)
{
	if (!FB_BASE)
		return;
	volatile uint32_t *fb = (volatile uint32_t *)FB_BASE;
	for (int y = y0; y < y0 + h && y < FB_H; y++)
		for (int x = x0; x < x0 + w && x < FB_W; x++)
			fb[y * (FB_STRIDE / 4) + x] = c;
}

static void scr_ch(char c)
{
	if (!FB_BASE)
		return;
	if (c == '\n' || col >= COLS) {
		col = 0;
		row = (row + 1) % ROWS;
		fill(0, 120 + row * CELL_H, FB_W, CELL_H, 0xff000000U);
		if (c == '\n')
			return;
	}
	if (c < 0x20 || c > 0x7e)
		c = '?';
	const unsigned char *g = font8x16[c - 0x20];
	int x0 = 24 + col * CELL_W, y0 = 120 + row * CELL_H;
	for (int gy = 0; gy < 16; gy++)
		for (int gx = 0; gx < 8; gx++)
			fill(x0 + gx * SCALE, y0 + gy * SCALE, SCALE, SCALE,
			     (g[gy] & (0x80 >> gx)) ? fg : 0xff000000U);
	col++;
}

static void out(const char *s)
{
	while (*s) {
		log_ch(*s);
		scr_ch(*s++);
	}
}

static void hex(uint64_t v)
{
	static const char digits[] = "0123456789abcdef";
	char b[19] = "0x";
	int n = 2, started = 0;
	for (int i = 60; i >= 0; i -= 4) {
		int d = (int)((v >> i) & 0xf);
		if (d || started || i == 0) {
			b[n++] = digits[d];
			started = 1;
		}
	}
	b[n] = 0;
	out(b);
}

static void kv(const char *k, uint64_t v)
{
	out(k);
	out("=");
	hex(v);
	out(" ");
}

#define SYSREG(name) ({ uint64_t _v; __asm__ volatile("mrs %0, " #name : "=r"(_v)); _v; })

static void clean(uintptr_t start, uintptr_t end)
{
	uint64_t ctr = SYSREG(ctr_el0);
	uintptr_t line = 4UL << ((ctr >> 16) & 0xf);
	for (uintptr_t p = start & ~(line - 1); p < end; p += line)
		__asm__ volatile("dc civac, %0" : : "r"(p) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

void dl_main(uintptr_t dtb, uint64_t el, uint64_t sctlr)
{
	if (DECON_TRIG_ADDR)
		*(volatile uint32_t *)DECON_TRIG_ADDR = DECON_TRIG_VAL;
	if (RAMOOPS_CONSOLE) {
		prz = (volatile struct prz *)RAMOOPS_CONSOLE;
		prz->sig = PRZ_SIG;
		prz->start = prz->size = 0;
	}

	/* A solid colour first: if the screen turns blue, S-Boot ran our code and
	 * FB_BASE is right, even if the text below turns out garbled. */
	fill(0, 0, FB_W, FB_H, 0xff0000a0U);
	fill(0, 0, FB_W, FB_H, 0xff000000U);
	fg = 0xffffff00U;
	out("NOTE5 PROBE BEGIN (ioson-note5 stage 1)\n");
	fg = 0xffffffffU;

	kv("EL", el);
	kv("SCTLR", sctlr);
	out("\n");
	kv("x0", dtb);
	/* A flattened device tree starts with 0xd00dfeed (big endian); read as a
	 * little-endian word that is 0xedfe0dd0. */
	if (dtb && !(dtb & 3))
		kv("x0word", *(volatile uint32_t *)dtb);
	out("\n");
	kv("MIDR", SYSREG(midr_el1));
	kv("MPIDR", SYSREG(mpidr_el1));
	out("\n");
	kv("MMFR0", SYSREG(id_aa64mmfr0_el1));
	kv("ISAR0", SYSREG(id_aa64isar0_el1));
	out("\n");
	kv("CNTFRQ", SYSREG(cntfrq_el0));
	kv("CTR", SYSREG(ctr_el0));
	out("\n");
	kv("FB", FB_BASE);
	kv("RAMOOPS", RAMOOPS_CONSOLE);
	out("\n\n");
	fg = 0xff40ff40U;
	out("PARKED. Nothing else was touched.\n");
	fg = 0xffffffffU;
	out("Hold POWER+VOL DOWN+HOME to reboot, then\nPOWER+VOL UP+HOME for TWRP.\n");

	if (RAMOOPS_CONSOLE)
		clean(RAMOOPS_CONSOLE, RAMOOPS_CONSOLE + RAMOOPS_CONSOLE_SIZE);
}

void dl_exception(uint64_t slot)
{
	fg = 0xffff4040U;
	out("\nLOADER EXCEPTION ");
	kv("slot", slot);
	kv("ESR", SYSREG(esr_el1));
	kv("ELR", SYSREG(elr_el1));
	kv("FAR", SYSREG(far_el1));
	out("\n");
	if (RAMOOPS_CONSOLE)
		clean(RAMOOPS_CONSOLE, RAMOOPS_CONSOLE + RAMOOPS_CONSOLE_SIZE);
	for (;;)
		__asm__ volatile("wfe");
}

/* The compiler may emit calls for struct copies; keep the image self-contained. */
void *memset(void *d, int c, unsigned long n)
{
	unsigned char *q = d;
	while (n--)
		*q++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
	unsigned char *q = d;
	const unsigned char *r = s;
	while (n--)
		*q++ = *r++;
	return d;
}
