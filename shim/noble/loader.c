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
#include "discover.h"
#include "../font.h"

/* ---- persistent log (pstore ramoops console zone) ---- */
struct prz {
	uint32_t sig, start, size;
	uint8_t data[];
};
#define PRZ_SIG	0x43474244U	/* "DBGC" */
static volatile struct prz *prz;
static uint32_t log_len;
/* Sinks start from board.h; dl_main() fills in what is still 0 from the
 * device tree S-Boot passes (only from regions that tree itself lists). */
static uint64_t log_base = RAMOOPS_CONSOLE, log_size = RAMOOPS_CONSOLE_SIZE;
static uint64_t fb_base = FB_BASE;
/* Source of each sink as a small integer, not a string pointer: this image is
 * never relocated, so initialised data must not contain absolute addresses. */
enum { SRC_NONE, SRC_BOARD, SRC_DT, SRC_DT_RESV, SRC_DT_ARG };
static int log_src = RAMOOPS_CONSOLE ? SRC_BOARD : SRC_NONE;
static int fb_src = FB_BASE ? SRC_BOARD : SRC_NONE;

static void log_ch(char c)
{
	if (!prz || log_size < 12 || log_len >= log_size - 12)
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
	if (!fb_base)
		return;
	volatile uint32_t *fb = (volatile uint32_t *)fb_base;
	for (int y = y0; y < y0 + h && y < FB_H; y++)
		for (int x = x0; x < x0 + w && x < FB_W; x++)
			fb[y * (FB_STRIDE / 4) + x] = c;
}

static void scr_ch(char c)
{
	if (!fb_base)
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

static void out_src(int s)
{
	if (s == SRC_BOARD)
		out("board.h");
	else if (s == SRC_DT)
		out("dt");
	else if (s == SRC_DT_RESV)
		out("dt-resv");
	else if (s == SRC_DT_ARG)
		out("dt-arg");
	else
		out("none");
}

static struct dtinfo dt;
static int dt_ok;

static int in_known(uint64_t a, uint64_t n)
{
	for (int i = 0; i < dt.nmem; i++)
		if (a >= dt.mem[i].base && n <= dt.mem[i].size && a - dt.mem[i].base <= dt.mem[i].size - n)
			return 1;
	for (int i = 0; i < dt.nresv; i++)
		if (a >= dt.resv[i].base && n <= dt.resv[i].size && a - dt.resv[i].base <= dt.resv[i].size - n)
			return 1;
	return 0;
}

/* Use device-tree facts only where board.h has none, and only for addresses
 * inside RAM / reserved regions that the tree itself lists: writing to an
 * unlisted address with the MMU off could fault with nothing to report it. */
static void pick_sinks(void)
{
	if (!log_base && dt.zones_ok && dt.console_size >= 64 && in_known(dt.zone_console, dt.console_size)) {
		log_base = dt.zone_console;
		log_size = dt.console_size;
		log_src = SRC_DT;
	}
	if (!fb_base) {
		if (dt.has_fb_resv && dt.fb_resv.size >= (uint64_t)FB_STRIDE * FB_H) {
			fb_base = dt.fb_resv.base;
			fb_src = SRC_DT_RESV;
		} else if (dt.has_fb_arg && in_known(dt.fb_arg, (uint64_t)FB_STRIDE * FB_H)) {
			fb_base = dt.fb_arg;
			fb_src = SRC_DT_ARG;
		}
	}
}

static void report_dt(uintptr_t dtb)
{
	if (!dtb || (dtb & 3)) {
		out("DT: x0 is not a usable pointer\n");
		return;
	}
	if (!dt_ok) {
		out("DT: not a valid tree ");
		kv("err", (uint64_t)(-(int64_t)dt.err));
		out("\n");
		return;
	}
	out("DT ok ");
	kv("size", dt.totalsize);
	out("\n");
	for (int i = 0; i < dt.nmem; i++) {
		out("RAM ");
		kv("b", dt.mem[i].base);
		kv("s", dt.mem[i].size);
		out("\n");
	}
	for (int i = 0; i < dt.nresv && i < 10; i++) {
		out(dt.resv[i].name);
		out(" ");
		kv("b", dt.resv[i].base);
		kv("s", dt.resv[i].size);
		out("\n");
	}
	if (dt.zones_ok) {
		out("ramoops console ");
		kv("b", dt.zone_console);
		kv("s", dt.console_size);
		out("\n");
	} else if (dt.has_ramoops) {
		out("ramoops found, zone layout unknown\n");
	}
	if (dt.has_fb_arg) {
		out("fb arg ");
		kv("b", dt.fb_arg);
		out("\n");
	}
	out("CPUs:");
	for (int i = 0; i < dt.ncpu; i++) {
		out(" ");
		hex(dt.cpu_reg[i]);
	}
	out("\nargs: ");
	out(dt.bootargs);
	out("\n");
}

void dl_main(uintptr_t dtb, uint64_t el, uint64_t sctlr)
{
	if (dtb && !(dtb & 3))
		dt_ok = dt_discover(dtb, &dt);
	if (dt_ok)
		pick_sinks();
	if (DECON_TRIG_ADDR)
		*(volatile uint32_t *)DECON_TRIG_ADDR = DECON_TRIG_VAL;
	if (log_base) {
		prz = (volatile struct prz *)log_base;
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
	kv("FB", fb_base);
	kv("LOG", log_base);
	out("\nfb from: ");
	out_src(fb_base ? fb_src : SRC_NONE);
	out("  log from: ");
	out_src(log_base ? log_src : SRC_NONE);
	out("\n");
	report_dt(dtb);
	out("\n");
	fg = 0xff40ff40U;
	out("PARKED. Nothing else was touched.\n");
	fg = 0xffffffffU;
	out("Hold POWER+VOL DOWN+HOME to reboot, then\nPOWER+VOL UP+HOME for TWRP.\n");

	if (log_base)
		clean(log_base, log_base + log_size);
}

void dl_exception(uint64_t slot)
{
	static volatile int again;
	if (again)		/* the report itself faulted: stop, do not loop */
		for (;;)
			__asm__ volatile("wfe");
	again = 1;
	fg = 0xffff4040U;
	out("\nLOADER EXCEPTION ");
	kv("slot", slot);
	kv("ESR", SYSREG(esr_el1));
	kv("ELR", SYSREG(elr_el1));
	kv("FAR", SYSREG(far_el1));
	out("\n");
	if (log_base)
		clean(log_base, log_base + log_size);
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
