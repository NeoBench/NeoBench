OUTPUT_ARCH(m68k)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5); /* R + X */
    data PT_LOAD FLAGS(6); /* R + W */
}

SECTIONS
{
    . = 0x00080000;

    .text : ALIGN(4)
    {
        __loader_text_start = .;

        KEEP(*(.text.start))
        *(.text .text.*)
        *(.rodata .rodata.*)

        __loader_text_end = .;
    } :text

    .data : ALIGN(4)
    {
        __loader_data_start = .;

        *(.data .data.*)

        __loader_data_end = .;
    } :data

    .bss (NOLOAD) : ALIGN(4)
    {
        __loader_bss_start = .;

        *(.bss .bss.*)
        *(COMMON)

        . = ALIGN(4);

        __loader_bss_end = .;
        __loader_end = .;
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }
}
