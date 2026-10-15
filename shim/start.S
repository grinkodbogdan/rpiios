/*
 * Raspberry Pi 3 boot shim entry: what the VideoCore firmware jumps to as
 * kernel7.img (32-bit). Firmware contract: r0 = 0, r1 = machine type,
 * r2 = ATAG list or device tree blob; MMU off; possibly in HYP mode.
 *
 * We drop to SVC with interrupts masked, copy ourselves to SHIM_LINK (so the
 * load address does not matter: the firmware uses 0x8000, QEMU's -bios also
 * 0x8000), set up a stack and call shim_main.
 */
    .syntax unified
    .arm
    .section .text.start, "ax"
    .global _start
_start:
    /* Only core 0 boots. The real firmware already parks cores 1-3 in its
     * spin table; QEMU's -bios starts them here, so park them ourselves. */
    mrc     p15, 0, r12, c0, c0, 5      @ MPIDR
    ands    r12, r12, #3
    beq     core0
park:
    wfe
    b       park
core0:
    mov     r4, r0
    mov     r5, r1
    mov     r6, r2
    mrs     r7, cpsr                    @ entry CPSR, for the handoff
    mrc     p15, 0, r8, c1, c0, 0       @ entry SCTLR

    and     r0, r7, #0x1f
    cmp     r0, #0x1a                   @ HYP mode?
    bne     in_svc
    /* Leaving HYP for good: make sure nothing PL1 does traps to it.
     * HCPTR: no FP/NEON/trace traps. HSTR: no CP15 traps. HCR: all off. */
    mrc     p15, 4, r0, c1, c1, 2
    bic     r0, r0, #0xc00              @ TCP10, TCP11
    bic     r0, r0, #(1 << 15)          @ TASE
    bic     r0, r0, #(1 << 20)          @ TTA
    mcr     p15, 4, r0, c1, c1, 2
    mov     r0, #0
    mcr     p15, 4, r0, c1, c1, 3       @ HSTR
    mcr     p15, 4, r0, c1, c1, 0       @ HCR
    isb
    mrs     r0, cpsr
    bic     r0, r0, #0x1f
    orr     r0, r0, #0x13               @ SVC
    orr     r0, r0, #0x1c0              @ A/I/F masked
    msr     spsr_hyp, r0
    adr     r0, in_svc
    msr     elr_hyp, r0
    eret

in_svc:
    cpsid   aif
    /* MMU, D-cache and alignment checking off; I-cache state is irrelevant
     * because we invalidate it after every copy of code. */
    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #(1 << 0)           @ M
    bic     r0, r0, #(1 << 2)           @ C
    bic     r0, r0, #(1 << 1)           @ A
    /* ARMv8 AArch32 (Cortex-A53) compatibility for 2013-era ARMv7 code:
     * CP15BEN enables the CP15 barrier operations the kernel uses;
     * ITD and SED clear keep all Thumb IT forms and SETEND legal.
     * Harmless on ARMv7 cores. The kernel only read-modify-writes SCTLR. */
    orr     r0, r0, #(1 << 5)           @ CP15BEN
    bic     r0, r0, #(1 << 7)           @ ITD
    bic     r0, r0, #(1 << 8)           @ SED
    mcr     p15, 0, r0, c1, c0, 0
    isb

    /* Copy the shim image to its link address. */
    adr     r9, _start                  @ where we are
    ldr     r10, =_start                @ where we were linked
    ldr     r11, =__image_end
    sub     r11, r11, r10               @ bytes to copy
    cmp     r9, r10
    beq     relocated
    mov     r0, r10
    mov     r1, r9
    mov     r2, r11
1:  ldr     r3, [r1], #4
    str     r3, [r0], #4
    subs    r2, r2, #4
    bgt     1b
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0       @ ICIALLU
    mcr     p15, 0, r0, c7, c5, 6       @ BPIALL
    dsb
    isb
relocated:
    /* Clear .bss. */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
2:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     2b

    ldr     sp, =__stack_top
    /* shim_main(load address, r2 from firmware, entry cpsr, entry sctlr) */
    mov     r0, r9
    mov     r1, r6
    mov     r2, r7
    mov     r3, r8
    ldr     r12, =shim_main
    blx     r12
3:  wfi
    b       3b

/*
 * enter_kernel(entry, boot_args): leave with caches/predictors invalidated
 * and r0 = physical boot_args, as XNU's _start expects.
 */
    .global enter_kernel
enter_kernel:
    mov     r4, r0
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0       @ ICIALLU
    mcr     p15, 0, r0, c7, c5, 6       @ BPIALL
    mcr     p15, 0, r0, c8, c7, 0       @ TLBIALL
    dsb
    isb
    mov     r0, r1
    mov     r1, #0
    mov     r2, #0
    bx      r4

    .ltorg
