OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_Reset)
PHDRS
{
	code   PT_LOAD FLAGS(5) /* Read | Execute */;
	rodata PT_LOAD FLAGS(4) /* Read */;
	data   PT_LOAD FLAGS(6) /* Read | Write */;
}
SECTIONS
{
	. = 0x00100100;
	. = ALIGN(4);
	.text : {
		obj/bootloader.o (.text*)
		*(.text*)
	} : code

	. = ALIGN(4);
	.rodata : {
		*(.rodata)
		*(.roda)
		*(.rodata.*)
		*all.rodata*(*)
	} : rodata

	. = ALIGN(4);
	.rel.dyn : {
		__rel_dyn_start = .;
		*(.rel)
		*(.rel.*)
		__rel_dyn_end = .;
	} : rodata

	. = ALIGN(4); .dynamic : { *(.dynamic) } : data
	. = ALIGN(4); .got : { *(.got.plt) *(.igot.plt) *(.got) *(.igot) } : data

	. = ALIGN(4);
	.data : {
		*(.data)
		*(.data.*)
	} : data

	. = ALIGN(4);
	.bss : {
		*(.__bss_start)
		*(.dynbss)
		*(.bss)
		*(.bss.*)
		*(COMMON)
		*(.__bss_end)
	}

	__end__ = .;
}
