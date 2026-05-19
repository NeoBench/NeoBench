ENTRY(_start)
SECTIONS
{
    . = 0x10000;
    .vectors : { *(.vectors) }
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}
