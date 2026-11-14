/* SPDX-License-Identifier: GPL-2.0-only
 * Minimal freestanding flattened-device-tree (FDT) walker for the Note 5 probe.
 * Byte-wise reads only (the probe runs with the MMU off, where unaligned
 * accesses fault) and every offset is bounds-checked against the header.
 */
#pragma once
#include <stdint.h>

enum { FDT_NODE_BEGIN, FDT_PROP, FDT_NODE_END };

struct fdt_ev {
	int type;
	int depth;		/* root node = 0 */
	const char *node;	/* current node name ("" for the root) */
	const char *parent;	/* parent node name, "" at depth 0 */
	const char *prop;	/* FDT_PROP only */
	const uint8_t *val;
	uint32_t len;
	uint32_t pac, psc;	/* #address-cells / #size-cells of the PARENT node */
};

typedef void (*fdt_cb)(void *ctx, const struct fdt_ev *ev);

/* 0 if the blob at fdt looks like a valid FDT header; *totalsize is set. */
int fdt_check(uintptr_t fdt, uint32_t *totalsize);
/* 0 on success, negative on a malformed blob (callbacks may already have run). */
int fdt_walk(uintptr_t fdt, fdt_cb cb, void *ctx);

uint32_t fdt_be32(const uint8_t *p);
/* Big-endian value of the last (up to two) of ncells 32-bit cells. */
uint64_t fdt_cells(const uint8_t *p, uint32_t ncells);
int fdt_streq(const char *a, const char *b);
int fdt_startswith(const char *s, const char *prefix);
int fdt_contains(const char *hay, const char *needle);
