/*
 * Xiaomi Mi A1 (tissot) boot shim: entry.
 *
 * The stock bootloader (Qualcomm LK "aboot") boots this like an arm64 Linux
 * kernel from an Android boot image: it checks the Image header's magic, puts
 * the image somewhere in RAM and jumps to its first instruction at EL1 with
 * the MMU and data cache off and x0 = the device tree it chose (the one
 * appended to the image, matched by qcom,msm-id / qcom,board-id).
 *
 * The image is linked at 0 and relocates itself (R_AARCH64_RELATIVE only), so
 * it runs wherever aboot puts it.
 */
	.section .text.head, "ax"
	.globl _head
_head:
	b	start                   /* code0 */
	.long	0                       /* code1 */
	.quad	0                       /* text_offset */
	.quad	_image_end - _head      /* image_size */
	.quad	0x2                     /* flags: little endian, 4 KiB pages */
	.quad	0, 0, 0                 /* res2..res4 */
	.ascii	"ARM\x64"               /* magic */
	.long	0                       /* res5 */

start:
	mov	x19, x0                 /* device tree */
	msr	daifset, #0xf
	/* FP and SIMD on, as UEFI has them: the loader saves and restores those registers */
	mov	x1, #(3 << 20)
	msr	cpacr_el1, x1
	isb
	adr	x1, _stack_top
	mov	sp, x1
	/* relocate: each R_AARCH64_RELATIVE entry gets the load address added */
	adr	x20, _head
	adr	x1, __rela_start
	adr	x2, __rela_end
1:	cmp	x1, x2
	b.hs	3f
	ldp	x3, x4, [x1], #16       /* r_offset, r_info */
	ldr	x5, [x1], #8            /* r_addend */
	cmp	w4, #1027
	b.ne	1b
	add	x5, x5, x20
	str	x5, [x20, x3]
	b	1b
	/* .bss */
3:	adr	x1, _bss_start
	adr	x2, _bss_end
4:	cmp	x1, x2
	b.hs	5f
	str	xzr, [x1], #8
	b	4b
5:	adr	x1, shim_vectors
	msr	vbar_el1, x1
	isb
	mov	x0, x19
	mov	x1, x20
	bl	shim_main
6:	wfi
	b	6b

/*
 * Exceptions taken in the shim or in the loader it runs (before the loader puts
 * its own vectors in): the registers, on the screen, and a stop.
 */
	.macro	vector kind
	.balign	0x80
	sub	sp, sp, #272
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x29, [sp, #224]
	str	x30, [sp, #240]
	mov	x0, #\kind
	mov	x1, sp
	bl	shim_exception
7:	wfi
	b	7b
	.endm

	.balign	0x800
	.globl	shim_vectors
shim_vectors:
	vector	0
	vector	1
	vector	2
	vector	3
	vector	4
	vector	5
	vector	6
	vector	7
	vector	8
	vector	9
	vector	10
	vector	11
	vector	12
	vector	13
	vector	14
	vector	15
