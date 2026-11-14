/* SPDX-License-Identifier: GPL-2.0-only
 * See discover.h. Walks the FDT once and fills struct dtinfo.
 */
#include "discover.h"
#include "fdt.h"

struct state {
	struct dtinfo *d;
	int cur_resv;		/* index into d->resv for the node being walked, or -1 */
};

static void copy_str(char *dst, const char *src, uint32_t max_src, int dst_max)
{
	int i = 0;
	while (i < dst_max - 1 && (uint32_t)i < max_src && src[i]) {
		dst[i] = src[i];
		i++;
	}
	dst[i] = 0;
}

static uint32_t scalar(const struct fdt_ev *e)
{
	return (uint32_t)fdt_cells(e->val, e->len / 4);
}

static void on_event(void *ctx, const struct fdt_ev *e)
{
	struct state *s = ctx;
	struct dtinfo *d = s->d;

	if (e->type == FDT_NODE_BEGIN) {
		if (e->depth == 2 && fdt_streq(e->parent, "reserved-memory")) {
			if (d->nresv < DT_MAX_RESV) {
				struct dtregion *r = &d->resv[d->nresv];
				copy_str(r->name, e->node, 0xffff, DT_NAME);
				r->base = r->size = 0;
				s->cur_resv = d->nresv++;
			} else {
				s->cur_resv = -1;
			}
		}
		return;
	}
	if (e->type == FDT_NODE_END) {
		if (e->depth == 2)
			s->cur_resv = -1;
		return;
	}

	/* FDT_PROP */
	if (e->depth == 1 && fdt_streq(e->node, "chosen")) {
		if (fdt_streq(e->prop, "bootargs")) {
			copy_str(d->bootargs, (const char *)e->val, e->len, DT_BOOTARGS);
		} else if (fdt_streq(e->prop, "linux,initrd-start") && e->len >= 4) {
			d->initrd_start = fdt_cells(e->val, e->len / 4);
			d->has_initrd |= 1;
		} else if (fdt_streq(e->prop, "linux,initrd-end") && e->len >= 4) {
			d->initrd_end = fdt_cells(e->val, e->len / 4);
			d->has_initrd |= 2;
		}
	} else if (e->depth == 1 && (fdt_streq(e->node, "memory") || fdt_startswith(e->node, "memory@")) &&
		   fdt_streq(e->prop, "reg")) {
		uint32_t step = (e->pac + e->psc) * 4;
		for (uint32_t off = 0; step && off + step <= e->len && d->nmem < DT_MAX_MEM; off += step) {
			struct dtregion *r = &d->mem[d->nmem++];
			copy_str(r->name, e->node, 0xffff, DT_NAME);
			r->base = fdt_cells(e->val + off, e->pac);
			r->size = fdt_cells(e->val + off + e->pac * 4, e->psc);
		}
	} else if (e->depth == 2 && s->cur_resv >= 0 && fdt_streq(e->parent, "reserved-memory")) {
		struct dtregion *r = &d->resv[s->cur_resv];
		if (fdt_streq(e->prop, "reg") && e->len >= (e->pac + e->psc) * 4) {
			r->base = fdt_cells(e->val, e->pac);
			r->size = fdt_cells(e->val + e->pac * 4, e->psc);
		} else if (fdt_contains(r->name, "ramoops")) {
			if (fdt_streq(e->prop, "record-size"))
				d->record_size = scalar(e);
			else if (fdt_streq(e->prop, "console-size"))
				d->console_size = scalar(e);
			else if (fdt_streq(e->prop, "ftrace-size"))
				d->ftrace_size = scalar(e);
			else if (fdt_streq(e->prop, "pmsg-size"))
				d->pmsg_size = scalar(e);
		}
	} else if (e->depth == 2 && fdt_streq(e->parent, "cpus") && fdt_streq(e->prop, "reg") &&
		   e->len >= 4 && d->ncpu < DT_MAX_CPU) {
		d->cpu_reg[d->ncpu++] = fdt_cells(e->val, e->len / 4);
	}
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* First "<key>=0x<hex>" token in bootargs whose key contains "fb". */
static void scan_bootargs(struct dtinfo *d)
{
	const char *p = d->bootargs;
	while (*p) {
		while (*p == ' ')
			p++;
		const char *tok = p;
		while (*p && *p != ' ')
			p++;
		const char *eq = tok;
		while (eq < p && *eq != '=')
			eq++;
		if (eq == p || eq + 3 > p || eq[1] != '0' || eq[2] != 'x')
			continue;
		int has_fb = 0;
		for (const char *k = tok; k + 1 < eq; k++)
			if (k[0] == 'f' && k[1] == 'b')
				has_fb = 1;
		if (!has_fb)
			continue;
		uint64_t v = 0;
		const char *h = eq + 3;
		int n = 0;
		while (h < p && hexval(*h) >= 0 && n < 16) {
			v = (v << 4) | (uint64_t)hexval(*h);
			h++;
			n++;
		}
		if (n) {
			d->fb_arg = v;
			d->has_fb_arg = 1;
			return;
		}
	}
}

/* Zone layout of Linux's ramoops driver: dump zones first (record_size each,
 * as many as fit in what is left after console + ftrace + pmsg), then console,
 * ftrace, pmsg. A record_size of 0 means no dump zones. */
static void compute_zones(struct dtinfo *d)
{
	uint64_t fixed = (uint64_t)d->console_size + d->ftrace_size + d->pmsg_size;
	if (!d->has_ramoops || !d->console_size || fixed > d->ramoops.size)
		return;
	uint64_t dump = d->ramoops.size - fixed, used = 0;
	if (d->record_size) {
		uint64_t cnt = dump / d->record_size;
		if (!cnt)
			return;
		used = cnt * (dump / cnt);
	}
	d->zone_console = d->ramoops.base + used;
	d->zone_ftrace = d->zone_console + d->console_size;
	d->zone_pmsg = d->zone_ftrace + d->ftrace_size;
	d->zones_ok = 1;
}

int dt_discover(uintptr_t fdt, struct dtinfo *d)
{
	struct state s = { d, -1 };
	d->ok = 0;
	d->err = fdt_check(fdt, &d->totalsize);
	if (d->err)
		return 0;
	d->err = fdt_walk(fdt, on_event, &s);
	if (d->err)
		return 0;
	d->ok = 1;
	scan_bootargs(d);
	for (int i = 0; i < d->nresv; i++) {
		struct dtregion *r = &d->resv[i];
		if (!d->has_ramoops && fdt_contains(r->name, "ramoops") && r->size) {
			d->ramoops = *r;
			d->has_ramoops = 1;
		}
		if (!d->has_fb_resv && r->size &&
		    (fdt_contains(r->name, "fb") || fdt_contains(r->name, "framebuffer") ||
		     fdt_contains(r->name, "lcd"))) {
			d->fb_resv = *r;
			d->has_fb_resv = 1;
		}
	}
	compute_zones(d);
	return 1;
}
