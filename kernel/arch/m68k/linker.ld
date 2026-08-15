ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x1000;

    .text :
    {
        KEEP(*(.text))
        *(.text*)
    } :text

    .rodata :
    {
        *(.rodata*)
    } :text

    .data :
    {
        . = ALIGN(4);
        *(.data*)
    } :data

    .bss (NOLOAD) :
    {
        . = ALIGN(4);

        _bss_start = .;

        *(.bss*)
        *(COMMON)

        _bss_end = .;
    } :data

    . = ALIGN(4);

    _kernel_end = .;
}
