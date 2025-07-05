ENTRY(rom_boot_check)

SECTIONS {
    . = 0xF80000; /* ROM base address for 512KB BIOS */

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame)
    }
}
