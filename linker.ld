ENTRY(_start)

MEMORY
{
    ROM (rx) : ORIGIN = 0x00F80000, LENGTH = 512K
    RAM (rw) : ORIGIN = 0x00000000, LENGTH = 4M
}

SECTIONS
{
    .vectors ORIGIN(ROM) :
    {
        KEEP(*(.vectors*))
    } > ROM

    .text :
    {
        *(.text*)
        *(.rodata*)
    } > ROM

    .data :
    {
        *(.data*)
    } > RAM

    .bss (NOLOAD) :
    {
        *(.bss*)
        *(COMMON)
    } > RAM
}
