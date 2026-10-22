ENTRY(_head)
SECTIONS
{
	. = 0;
	.text : { *(.text.head) *(.text*) }
	.rodata : { *(.rodata*) }
	.data.rel.ro : { *(.data.rel.ro*) }
	.data : { *(.data*) *(.got*) }
	. = ALIGN(8);
	.rela.dyn : { __rela_start = .; *(.rela*) __rela_end = .; }
	.dynamic : { *(.dynamic) }
	. = ALIGN(4096);
	_bss_start = .;
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(16);
	_bss_end = .;
	. = . + 0x10000;
	_stack_top = .;
	_image_end = .;
	/DISCARD/ : { *(.dynsym) *(.dynstr) *(.hash) *(.gnu.hash) *(.interp) *(.comment) }
}
