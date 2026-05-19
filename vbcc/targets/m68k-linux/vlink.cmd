
MEMORY
{
ram : org = 0x1800040, l = 0xffffff
}


SECTIONS
{
	.debug_abbrev 0 : { *(.debug_abbrev) } 
	.debug_info 0 : { *(.debug_info) }
	.debug_line 0 : { *(.debug_line) }
	.debug_pubnames 0 : { *(.debug_pubnames) }
	.debug_aranges 0 : { *(.debug_aranges) }
 	.text : { *(CODE) } > ram
	.dtors : { *(.dtors) } > ram
	.ctors : { *(.ctors) } > ram
	.data : { *(DATA) } > ram
	.bss : { *(BSS) } > ram
	__HEAPSTART = .;
}

