ENTRY(_start)

MEMORY
{
    ROM (rx) : ORIGIN = 0x00000000, LENGTH = 512K
}

SECTIONS
{
    .text : {
        *(.text*)
        *(.rodata*)
        *(.data*)
        . = ALIGN(4);
    } > ROM

    /DISCARD/ : {
        *(.eh_frame*)
    }
}
