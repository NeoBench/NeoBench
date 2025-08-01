ENTRY(_start)

SECTIONS
{
    /* Start address of the ROM */
    . = 0x000000;

    /* Text/code section - optimized for compact storage */
    .text :
    {
        *(.text.startup)  /* Startup code first */
        *(.text)          /* Regular code */
        *(.text.*)        /* Other code sections */
    }

    /* Read-only data (constants, strings) */
    .rodata :
    {
        *(.rodata)
        *(.rodata.*)
    }

    /* Boot device and hardware tables - kept close to code */
    .boot_tables :
    {
        *(.boot_tables)
    }

    /* Initialized data */
    .data :
    {
        *(.data)
        *(.data.*)
    }

    /* Uninitialized data */
    .bss :
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /* Assets section (logo, sounds) - can be compressed if needed */
    .assets :
    {
        *(.assets)
    }

    /* Discard unnecessary sections to save space */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }

    /* Hard limit - ROM cannot exceed 512KB */
    . = 0x80000;
}
