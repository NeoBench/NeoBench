ENTRY(_start)

SECTIONS
{
    /* Start address of the ROM */
    . = 0x000000;

    /* Text/code section */
    .text :
    {
        *(.text)
    }

    /* Read-only data (if used) */
    .rodata :
    {
        *(.rodata*)
    }

    /* Initialized data */
    .data :
    {
        *(.data)
    }

    /* Uninitialized data */
    .bss :
    {
        *(.bss)
        *(COMMON)
    }

    /* Pad to 512KB total ROM size */
    . = 0x80000;
}
