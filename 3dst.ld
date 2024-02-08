/*--------------------------------------------------------------------------------
	This Source Code Form is subject to the terms of the Mozilla Public License,
	v. 2.0. If a copy of the MPL was not distributed with this file, You can
	obtain one at https://mozilla.org/MPL/2.0/.
--------------------------------------------------------------------------------*/

/* Adapted from the original NTR 3ds.ld and devkitPro's 3dsx.ld */

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
		. = ALIGN(4);
		__tdata_align = .;
		LONG (ALIGNOF(.tdata));
		. = ALIGN(4);
	} : rodata

	. = ALIGN(4);
	.rel.dyn : {
		*(.__rel_dyn_start)
		*(.rel*)
		*(.rel.*)
		*(.__rel_dyn_end)
	} : rodata

	.ARM.exidx   :
	{
		PROVIDE_HIDDEN (__exidx_start = .);
		*(.ARM.exidx*)
		PROVIDE_HIDDEN (__exidx_end = .);
	}

	. = ALIGN(4);
	.data : {
		*(.data)
	} : data

	. = ALIGN(4);
	.tdata : {
		__tdata_lma = .;
		*(.tdata)
		*(.tdata.*)
		. = ALIGN(4);
		__tdata_lma_end = .;
	} : data

	. = ALIGN(4);
	.tbss :  {
		*(.tbss)
		*(.tbss.*)
		*(.tcommon)
		. = ALIGN(4);
	} : data

	. = ALIGN(4);
	.bss : {
		*(.__bss_start)
		*(.dynbss)
		*(.bss)
		*(.bss.*)
		*(COMMON)

		. = 8 + ABSOLUTE(ALIGN(ABSOLUTE(. - 8), ALIGNOF(.tdata)));
		__tls_start = .;
		. += SIZEOF(.tdata);

		. = ALIGN(ALIGNOF(.tbss));
		. += SIZEOF(.tbss);
		__tls_end = .;

		*(.__bss_end)
	} : data

	__end__ = ABSOLUTE(.) ;
}
