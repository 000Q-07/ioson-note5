/* SPDX-License-Identifier: GPL-2.0-only
 * Minimal freestanding FDT walker, see fdt.h. Format: devicetree specification
 * section 5 (header, structure block with BEGIN_NODE/END_NODE/PROP/NOP/END
 * tokens, strings block).
 */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeedU
#define FDT_MAX_SIZE	(16U << 20)
#define MAX_DEPTH	16

uint32_t fdt_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint64_t fdt_cells(const uint8_t *p, uint32_t ncells)
{
	if (ncells > 2) {
		p += (ncells - 2) * 4;
		ncells = 2;
	}
	uint64_t v = 0;
	for (uint32_t i = 0; i < ncells; i++)
		v = (v << 32) | fdt_be32(p + i * 4);
	return v;
}

int fdt_streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

int fdt_startswith(const char *s, const char *prefix)
{
	while (*prefix) {
		if (*s++ != *prefix++)
			return 0;
	}
	return 1;
}

int fdt_contains(const char *hay, const char *needle)
{
	if (!*needle)
		return 1;
	for (; *hay; hay++)
		if (fdt_startswith(hay, needle))
			return 1;
	return 0;
}

int fdt_check(uintptr_t fdt, uint32_t *totalsize)
{
	if (!fdt || (fdt & 3))
		return -1;
	const uint8_t *b = (const uint8_t *)fdt;
	if (fdt_be32(b) != FDT_MAGIC)
		return -2;
	uint32_t total = fdt_be32(b + 4);
	if (total < 0x28 || total > FDT_MAX_SIZE)
		return -3;
	if (totalsize)
		*totalsize = total;
	return 0;
}

int fdt_walk(uintptr_t fdt, fdt_cb cb, void *ctx)
{
	uint32_t total;
	if (fdt_check(fdt, &total))
		return -1;
	const uint8_t *b = (const uint8_t *)fdt;
	uint32_t off_struct = fdt_be32(b + 8), off_str = fdt_be32(b + 12);
	uint32_t size_str = fdt_be32(b + 32), size_struct = fdt_be32(b + 36);
	if (off_struct > total || size_struct > total - off_struct ||
	    off_str > total || size_str > total - off_str)
		return -2;

	const char *names[MAX_DEPTH];
	uint32_t ac[MAX_DEPTH], sc[MAX_DEPTH];
	int depth = -1;
	uint32_t p = off_struct, end = off_struct + size_struct;

	while (p + 4 <= end) {
		uint32_t tok = fdt_be32(b + p);
		p += 4;
		struct fdt_ev ev = { 0 };
		if (tok == 1) {			/* FDT_BEGIN_NODE */
			uint32_t n = 0;
			while (p + n < end && b[p + n])
				n++;
			if (p + n >= end)
				return -3;
			if (depth + 1 >= MAX_DEPTH)
				return -4;
			depth++;
			names[depth] = (const char *)(b + p);
			ac[depth] = 2;		/* spec defaults until the node says otherwise */
			sc[depth] = 1;
			p += (n + 1 + 3) & ~3U;
			ev.type = FDT_NODE_BEGIN;
		} else if (tok == 2) {		/* FDT_END_NODE */
			if (depth < 0)
				return -5;
			ev.type = FDT_NODE_END;
		} else if (tok == 3) {		/* FDT_PROP */
			if (depth < 0 || p + 8 > end)
				return -6;
			uint32_t len = fdt_be32(b + p), nameoff = fdt_be32(b + p + 4);
			p += 8;
			if (len > end - p || nameoff >= size_str)
				return -7;
			ev.type = FDT_PROP;
			ev.val = b + p;
			ev.len = len;
			ev.prop = (const char *)(b + off_str + nameoff);
			p += (len + 3) & ~3U;
			if (p > end)
				return -8;
			if (fdt_streq(ev.prop, "#address-cells") && len == 4)
				ac[depth] = fdt_be32(ev.val);
			else if (fdt_streq(ev.prop, "#size-cells") && len == 4)
				sc[depth] = fdt_be32(ev.val);
		} else if (tok == 4) {		/* FDT_NOP */
			continue;
		} else if (tok == 9) {		/* FDT_END */
			break;
		} else {
			return -9;
		}
		ev.depth = depth;
		ev.node = names[depth];
		ev.parent = depth > 0 ? names[depth - 1] : "";
		ev.pac = depth > 0 ? ac[depth - 1] : 2;
		ev.psc = depth > 0 ? sc[depth - 1] : 1;
		cb(ctx, &ev);
		if (tok == 2)
			depth--;
	}
	return 0;
}
