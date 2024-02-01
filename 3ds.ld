OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_Reset)
SECTIONS
{
	. = 0x00100100;
	. = ALIGN(4);
	.text : {
		__text_start = .;
		obj/bootloader.o (.text*)
		*(.text*)
	}
	. = ALIGN(4);
	.data : {
		*(.data)
	}

	. = ALIGN(4);
	.rel.dyn : {
		*(.__rel_dyn_start)
		*(.rel*)
		*(.rel.*)
	*(.__rel_dyn_end)
	}
	__code_end = .;

	. = ALIGN(4);
	.bss : {
		*(.__bss_start)
		*(.dynbss)
		*(.bss)
		*(.bss.*)
		*(.gnu.linkonce.b*)
		*(COMMON)
		*(.__bss_end)
	}

	__end__ = .;
}
