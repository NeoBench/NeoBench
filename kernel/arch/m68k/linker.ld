ENTRY(_start)

PHDRS
{
    kernel PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x1000;

    .text :
    {
        KEEP(*(.text))
        *(.text*)
    } :kernel

    .rodata :
    {
        *(.rodata*)
    } :kernel

    .data :
    {
        *(.data*)
    } :kernel

    .bss :
    {
        _bss_start = .;

        *(.bss*)
        *(COMMON)

        _bss_end = .;
    } :kernel
}
