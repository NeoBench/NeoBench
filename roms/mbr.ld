ENTRY(mbr_entry)

SECTIONS
{
    /* Load all code/data starting at file-offset 0 */
    . = 0;

    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss COMMON) }

    /* Remove exception frames */
    /DISCARD/ : { *(.eh_frame) }
}
