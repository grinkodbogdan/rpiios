/*
 * Xiaomi Mi A1 (tissot) boot shim: entry.
 *
 * The stock bootloader (Qualcomm LK "aboot") boots this like an arm64 Linux
 * kernel from an Android boot image: it checks the Image header's magic, puts
 * the image somewhere in RAM and jumps to its first instruction at EL1 with
 * the MMU and data cache off and x0 = the device tree it chose (the one
 * appended to the image, matched by qcom,msm-id / qcom,board-id).
 *
 * Everything here is position independent: only PC-relative addressing.
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
	adr	x1, _stack_top
	mov	sp, x1
	/* .bss */
	adr	x1, _bss_start
	adr	x2, _bss_end
1:	cmp	x1, x2
	b.hs	2f
	str	xzr, [x1], #8
	b	1b
2:	mov	x0, x19
	bl	shim_main
3:	wfi
	b	3b
