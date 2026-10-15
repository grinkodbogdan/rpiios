/* The shim runs from 15 MiB: below the kernel (16 MiB up) and above the
 * payload, which stays where the firmware loaded kernel7.img (from 0x8000). */
ENTRY(_start)
SHIM_LINK = 0x00f00000;

SECTIONS
{
    . = SHIM_LINK;
    .text : { KEEP(*(.text.start)) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(4);
    __image_end = .;
    .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; }
    . = ALIGN(16);
    . += 0x4000;
    __stack_top = .;
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
ASSERT(__image_end - SHIM_LINK <= 0x10000, "shim image must fit in 64 KiB (payload offset)")
ASSERT(__stack_top <= 0x01000000, "shim must stay below the kernel's physical base")
