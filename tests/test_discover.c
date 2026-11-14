/* SPDX-License-Identifier: GPL-2.0-only
 * Host-side test of shim/noble/fdt.c + discover.c against synthetic device
 * trees (see make_fake_dtb.py). Runs on the build machine, not on a phone.
 *
 *   tests/run.sh
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../shim/noble/discover.h"
#include "../shim/noble/fdt.h"
#include "expected.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* Copy the blob so that its LAST byte is the last byte before an unmapped
 * guard page: any read past the end of the tree crashes the test (no
 * sanitizer needed). Blob sizes are multiples of 4 (the builder pads), so the
 * start is 4-aligned like a real FDT. */
#define MAXLIVE 8
static struct { uint8_t *p, *base; size_t len; } live[MAXLIVE];

static uint8_t *dup_exact(const uint8_t *src, size_t n)
{
	size_t pg = (size_t)sysconf(_SC_PAGESIZE);
	size_t span = (n + pg - 1) / pg * pg;
	uint8_t *m = mmap(NULL, span + pg, PROT_READ | PROT_WRITE,
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED || mprotect(m + span, pg, PROT_NONE) || n % 4)
		abort();
	uint8_t *p = m + span - n;
	memcpy(p, src, n);
	for (int i = 0; i < MAXLIVE; i++)
		if (!live[i].p) {
			live[i].p = p;
			live[i].base = m;
			live[i].len = span + pg;
			return p;
		}
	abort();
}

static void release(uint8_t *p)
{
	for (int i = 0; i < MAXLIVE; i++)
		if (live[i].p == p) {
			munmap(live[i].base, live[i].len);
			live[i].p = NULL;
			return;
		}
	abort();
}

static uint8_t *load(const char *dir, const char *name, size_t *n)
{
	char path[512];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "rb");
	if (!f) {
		perror(path);
		exit(2);
	}
	fseek(f, 0, SEEK_END);
	*n = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *tmp = malloc(*n);
	if (fread(tmp, 1, *n, f) != *n)
		exit(2);
	fclose(f);
	uint8_t *b = dup_exact(tmp, *n);
	free(tmp);
	return b;
}

static void test_full(const uint8_t *blob, size_t n)
{
	static struct dtinfo d;
	uint8_t *b = dup_exact(blob, n);
	memset(&d, 0, sizeof d);
	CHECK(dt_discover((uintptr_t)b, &d) == 1);
	CHECK(d.ok && d.err == 0 && d.totalsize == n);

	CHECK(d.nmem == 2);		/* memory-controller@ must not count as RAM */
	CHECK(d.mem[0].base == E_RAM0_BASE && d.mem[0].size == E_RAM0_SIZE);
	CHECK(d.mem[1].base == E_RAM1_BASE && d.mem[1].size == E_RAM1_SIZE);

	CHECK(d.nresv == 2);
	CHECK(d.has_ramoops);
	CHECK(d.ramoops.base == E_RAMOOPS_BASE && d.ramoops.size == E_RAMOOPS_SIZE);
	CHECK(d.record_size == E_REC && d.console_size == E_CON);
	CHECK(d.ftrace_size == E_FTR && d.pmsg_size == E_PMSG);
	CHECK(d.zones_ok);
	CHECK(d.zone_console == E_ZONE_CONSOLE);
	CHECK(d.zone_ftrace == E_ZONE_FTRACE);
	CHECK(d.zone_pmsg == E_ZONE_PMSG);

	CHECK(d.has_fb_resv && d.fb_resv.base == E_FBRESV_BASE && d.fb_resv.size == E_FBRESV_SIZE);
	CHECK(d.has_fb_arg && d.fb_arg == E_FBARG);

	static const uint64_t cpus[] = E_CPUS;
	CHECK(d.ncpu == E_NCPU);
	for (int i = 0; i < E_NCPU && i < d.ncpu; i++)
		CHECK(d.cpu_reg[i] == cpus[i]);

	CHECK(strcmp(d.bootargs, E_BOOTARGS) == 0);
	CHECK(d.has_initrd == 3 && d.initrd_start == E_INITRD_START && d.initrd_end == E_INITRD_END);
	release(b);
}

static void test_minimal(const uint8_t *blob, size_t n)
{
	static struct dtinfo d;
	uint8_t *b = dup_exact(blob, n);
	memset(&d, 0, sizeof d);
	CHECK(dt_discover((uintptr_t)b, &d) == 1);
	CHECK(d.nmem == 0 && d.nresv == 0 && d.ncpu == 0);
	CHECK(!d.has_ramoops && !d.zones_ok && !d.has_fb_arg && !d.has_fb_resv);
	CHECK(d.bootargs[0] == 0);
	release(b);
}

static void test_cells1(const uint8_t *blob, size_t n)
{
	static struct dtinfo d;
	uint8_t *b = dup_exact(blob, n);
	memset(&d, 0, sizeof d);
	CHECK(dt_discover((uintptr_t)b, &d) == 1);
	CHECK(d.nmem == 1 && d.mem[0].base == 0x80000000ULL && d.mem[0].size == 0x40000000ULL);
	release(b);
}

static void test_bad_input(const uint8_t *blob, size_t n)
{
	static struct dtinfo d;
	memset(&d, 0, sizeof d);
	CHECK(dt_discover(0, &d) == 0 && !d.ok);			/* x0 == 0 */

	uint8_t *b = dup_exact(blob, n);
	CHECK(dt_discover((uintptr_t)b + 1, &d) == 0);		/* unaligned */
	b[0] ^= 0xff;
	CHECK(dt_discover((uintptr_t)b, &d) == 0);			/* bad magic */
	b[0] ^= 0xff;
	release(b);

	/* declared size smaller than the structure block: must be rejected */
	b = dup_exact(blob, n);
	b[4] = b[5] = 0;
	b[6] = 0;
	b[7] = 0x30;
	CHECK(dt_discover((uintptr_t)b, &d) == 0 && d.err < 0);
	release(b);

	/* property length beyond the end of the structure block */
	b = dup_exact(blob, n);
	for (size_t i = 0x38; i + 12 < n; i += 4)
		if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 0 && b[i + 3] == 3) {
			b[i + 4] = 0x7f;				/* len = 0x7fxxxxxx */
			break;
		}
	CHECK(dt_discover((uintptr_t)b, &d) == 0);
	release(b);

	/* strings block whose last byte is not NUL: names could run past the end */
	b = dup_exact(blob, n);
	b[n - 1] = 'x';
	CHECK(dt_discover((uintptr_t)b, &d) == 0);
	release(b);
}

/* Corrupt a few bytes (never the header's totalsize field: on a phone that is
 * the only size information there is) and make sure nothing crashes or reads
 * out of bounds. ASan/UBSan do the checking. */
static void test_fuzz(const uint8_t *blob, size_t n, unsigned iters)
{
	static struct dtinfo d;
	uint64_t s = 0x9e3779b97f4a7c15ULL;
	unsigned accepted = 0;
	for (unsigned it = 0; it < iters; it++) {
		uint8_t *b = dup_exact(blob, n);
		int flips = 1 + (int)(s % 4);
		for (int k = 0; k < flips; k++) {
			s ^= s << 13; s ^= s >> 7; s ^= s << 17;
			size_t pos = (size_t)(s % n);
			if (pos >= 4 && pos < 8)
				continue;
			s ^= s << 13; s ^= s >> 7; s ^= s << 17;
			b[pos] = (uint8_t)s;
		}
		memset(&d, 0, sizeof d);
		accepted += (unsigned)dt_discover((uintptr_t)b, &d);
		release(b);
	}
	printf("fuzz: %u corrupted trees, %u still parsed; no crash, no read past the end (guard page)\n",
	       iters, accepted);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	unsigned iters = argc > 2 ? (unsigned)atoi(argv[2]) : 100000;
	size_t nf, nm, nc;
	uint8_t *full = load(dir, "full.dtb", &nf);
	uint8_t *mini = load(dir, "minimal.dtb", &nm);
	uint8_t *c1 = load(dir, "cells1.dtb", &nc);

	test_full(full, nf);
	test_minimal(mini, nm);
	test_cells1(c1, nc);
	test_bad_input(full, nf);
	test_fuzz(full, nf, iters);

	printf(fails ? "%d CHECK(S) FAILED\n" : "all checks passed\n", fails);
	return fails != 0;
}
