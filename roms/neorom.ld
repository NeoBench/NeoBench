ENTRY(mbr_entry)

SECTIONS
{
    /* Origin 0x7C00: where the BIOS loads the boot sector */
    . = 0x7C00;

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss COMMON) }

    /* BIOS boot signature at offset 0x1FE in the 512-byte sector */
    /DISCARD/ : { *(.eh_frame) }
}
