OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);     /* R + X */
    data PT_LOAD FLAGS(6);     /* R + W */
}

SECTIONS
{
    /*
 * The kernel is loaded as one flat image and runs at its link-time
 * addresses, so the VMA base must satisfy bp, given that ld lays the
 * first LOAD segment at file offset 0x1000:
 *
 *     VMA = KERNEL_BASE(0x40000000) + file_offset
 *
 * NeoBench targets an A4000 whose Zorro III RAM is autoconfiged by
 * FS-UAE at 0x40000000 (see scripts/build_hdf.py KERNEL_BASE and
 * fsuae/neoboot.fs-uae zorro_iii_memory).
 */
. = 0x40001000;

    .text : ALIGN(4)
    {
        __text_start = .;

        KEEP(*(.text.entry))
        KEEP(*(.text.start))
        *(.text*)
        *(.rodata*)

        . = ALIGN(4);
        __text_end = .;
    } :text

    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data*)

        . = ALIGN(4);
        __data_end = .;
    } :data

    .bss (NOLOAD) : ALIGN(4)
    {
        _bss_start = .;
        __bss_start = .;

        *(.bss*)
        *(COMMON)

        . = ALIGN(4);

        _bss_end = .;
        __bss_end = .;
    } :data

    . = ALIGN(16);

    _kernel_end = .;
    __kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gnu*)
    }
}
