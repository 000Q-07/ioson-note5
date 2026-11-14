/* SPDX-License-Identifier: GPL-2.0-only
 * Board-fact discovery from the device tree S-Boot passes to the probe.
 *
 * Everything here is READ from the DT; nothing is assumed about the Note 5.
 * Whatever the DT does not contain stays 0 / "not found".
 */
#pragma once
#include <stdint.h>

#define DT_MAX_MEM	8
#define DT_MAX_RESV	16
#define DT_MAX_CPU	16
#define DT_NAME		24
#define DT_BOOTARGS	512

struct dtregion {
	char name[DT_NAME];
	uint64_t base, size;
};

struct dtinfo {
	int ok;				/* 1 if a valid FDT was parsed completely */
	int err;			/* fdt_walk() result when !ok */
	uint32_t totalsize;

	struct dtregion mem[DT_MAX_MEM];
	int nmem;
	struct dtregion resv[DT_MAX_RESV];	/* /reserved-memory children */
	int nresv;
	uint64_t cpu_reg[DT_MAX_CPU];		/* /cpus/cpu@N "reg" (MPIDR) */
	int ncpu;

	char bootargs[DT_BOOTARGS];
	int has_initrd;
	uint64_t initrd_start, initrd_end;

	/* framebuffer hints: a "...fb=0x..." token in bootargs, or a reserved
	 * region whose name mentions fb / framebuffer / lcd */
	int has_fb_arg;
	uint64_t fb_arg;
	int has_fb_resv;
	struct dtregion fb_resv;

	/* ramoops reserved region and, if its properties allow, the pstore zones
	 * computed the way Linux's ramoops driver lays them out */
	int has_ramoops;
	struct dtregion ramoops;
	uint32_t record_size, console_size, ftrace_size, pmsg_size;
	int zones_ok;
	uint64_t zone_console, zone_ftrace, zone_pmsg;
};

/* Returns 1 on success (info->ok), 0 if x0 is not a usable FDT. */
int dt_discover(uintptr_t fdt, struct dtinfo *info);
