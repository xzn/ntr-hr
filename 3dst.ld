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
		__text_start = .;
		obj/bootloader.o (.text*)
		*(.text*)
	} : code
	. = ALIGN(0x1000);
	.rodata : {
		*(.rodata)
		*(.roda)
		*(.rodata.*)
		*all.rodata*(*)
		*(.gnu.linkonce.r*)
		. = ALIGN(4);
		__tdata_align = .;
		LONG (ALIGNOF(.tdata));
		. = ALIGN(4);
	} : rodata
	. = ALIGN(0x1000);
	.data : {
		*(.data)
	} : data
	. = ALIGN(4);
	.tdata : {
		__tdata_lma = .;
		*(.tdata)
		*(.tdata.*)
		*(.gnu.linkonce.td.*)
		. = ALIGN(4);
		__tdata_lma_end = .;
	} : data
	. = ALIGN(4);
	.tbss :  {
		*(.tbss)
		*(.tbss.*)
		*(.gnu.linkonce.tb.*)
		*(.tcommon)
		. = ALIGN(4);
	} : data

	.preinit_array ALIGN(4) :
	{
		PROVIDE (__preinit_array_start = .);
		KEEP (*(.preinit_array))
		PROVIDE (__preinit_array_end = .);
	} : data

	.init_array ALIGN(4) :
	{
		PROVIDE (__init_array_start = .);
		KEEP (*(SORT(.init_array.*)))
		KEEP (*(.init_array))
		PROVIDE (__init_array_end = .);
	} : data

	.fini_array ALIGN(4) :
	{
		PROVIDE (__fini_array_start = .);
		KEEP (*(.fini_array))
		KEEP (*(SORT(.fini_array.*)))
		PROVIDE (__fini_array_end = .);
	} : data

	.ctors ALIGN(4) :
	{
		KEEP (*crtbegin.o(.ctors)) /* MUST be first -- GCC requires it */
		KEEP (*(EXCLUDE_FILE (*crtend.o) .ctors))
		KEEP (*(SORT(.ctors.*)))
		KEEP (*(.ctors))
	} : data

	.dtors ALIGN(4) :
	{
		KEEP (*crtbegin.o(.dtors))
		KEEP (*(EXCLUDE_FILE (*crtend.o) .dtors))
		KEEP (*(SORT(.dtors.*)))
		KEEP (*(.dtors))
	} : data

	. = ALIGN(4);
	.rel.dyn : {
		*(.__rel_dyn_start)
		*(.rel*)
		*(.rel.*)
	*(.__rel_dyn_end)
	}
	__code_end = .;

	__bss_start__ = .;
	. = ALIGN(4);
	.bss : {
		*(.__bss_start)
		*(.dynbss)
		*(.bss)
		*(.bss.*)
		*(.gnu.linkonce.b*)
		*(COMMON)

		. = 8 + ABSOLUTE(ALIGN(ABSOLUTE(. - 8), ALIGNOF(.tdata)));
		__tls_start = .;
		. += SIZEOF(.tdata);

		. = ALIGN(ALIGNOF(.tbss));
        . += SIZEOF(.tbss);
		__tls_end = .;

		*(.__bss_end)
	} : data
	__bss_end__ = .;

	__end__ = ABSOLUTE(.) ;
}
