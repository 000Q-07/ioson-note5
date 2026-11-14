/* SPDX-License-Identifier: GPL-2.0-only
 * Galaxy Note 5 (Exynos 7420) stage-1 probe entry.
 *
 * Adapted from the Galaxy S8 loader (shim/dreamlte/start.S in
 * Floppikrob/ios-galaxy-s8). arm64 Linux Image header so Samsung S-Boot
 * treats us like a kernel. Nothing about the Note 5's entry state is assumed:
 * dl_main() reports CurrentEL, SCTLR_EL1 and the x0 value S-Boot passes, which
 * are exactly the facts the next stage needs. Position independent: the code
 * must run wherever S-Boot puts it.
 */
	.section .text.head, "ax"
	.globl _head
_head:
	b	_start			/* code0 */
	.long	0			/* code1 */
	.quad	0x80000			/* text_offset, as a stock arm64 kernel */
	.quad	_end			/* image_size (linked at 0) */
	.quad	0xa			/* flags: LE, 4K pages, place anywhere */
	.quad	0
	.quad	0
	.quad	0
	.ascii	"ARM\x64"		/* magic */
	.long	0

	.text
	.globl _start
_start:
	msr	daifset, #0xf
	mov	x19, x0			/* whatever S-Boot passed in x0 (DTB?) */
	mrs	x20, CurrentEL
	mrs	x21, sctlr_el1
	msr	spsel, #1
	adr	x1, _stack_top
	mov	sp, x1
	adr	x1, vectors
	msr	vbar_el1, x1
	isb
	adr	x1, _bss_start
	adr	x2, _bss_end
1:	cmp	x1, x2
	b.hs	2f
	str	xzr, [x1], #8
	b	1b
2:	mov	x0, x19
	lsr	x1, x20, #2		/* exception level number */
	mov	x2, x21
	bl	dl_main
3:	wfe				/* stage 1 never boots anything: park */
	b	3b

.macro vent slot
	.balign	0x80
	mov	x0, #\slot
	b	exc_common
.endm

	.balign	0x800
vectors:
	vent 0
	vent 1
	vent 2
	vent 3
	vent 4
	vent 5
	vent 6
	vent 7
	vent 8
	vent 9
	vent 10
	vent 11
	vent 12
	vent 13
	vent 14
	vent 15

exc_common:
	bl	dl_exception		/* never returns */
5:	wfe
	b	5b
