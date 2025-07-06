ENTRY(rom_boot_check)
SECTIONS {
    /* Link so that the end of this 512 KiB image
     * lands at 0x000FFFFF, i.e. starts at 0x000F0000 */
    . = 0x000F0000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss  : { *(.bss COMMON) }
    /DISCARD/ : { *(.eh_frame) }
}
