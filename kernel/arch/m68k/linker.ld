ENTRY(_start)

SECTIONS
{

. = 0x1000;


.text :
{
    KEEP(*(.text))
    *(.text*)
}


.rodata :
{
    *(.rodata*)
}


.data :
{
    *(.data*)
}


.bss :
{
    _bss_start = .;

    *(.bss*)
    *(COMMON)

    _bss_end = .;
}


}
