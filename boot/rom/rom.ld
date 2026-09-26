OUTPUT_FORMAT(elf32-m68k)
OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY
{
    /* 512 KiB kickstart region, A500/A1200-style. The processor reads its
     * reset vector table from the start of this image; code runs from $FC0000. */
    romvec (rx) : ORIGIN = 0x00F80000, LENGTH = 0x00040000
    rom    (rx) : ORIGIN = 0x00FC0000, LENGTH = 0x00040000

    /*
     * Chip RAM layout (2 MB, A1200):
     *   $010000..$060000  planar frame buffer (8 planes x $A000)
     *   $060000..$100000  RGB565 back buffer for the GUI compositor
     *   $100000..$110000  runtime arena (quantiser scratch)
     *   $110000..$1E0000  BSS / heap          (this region)
     *   $1E0000..$200000  stack, top at $1FFFE0
     */
    ram    (rw) : ORIGIN = 0x00110000, LENGTH = 0x000D0000
}

SECTIONS
{
    .bootvec :
    {
        KEEP(*(.bootvec))
    } > romvec

    .text :
    {
        KEEP(*(.text._start))
        *(.text*)
        *(.init*)
        *(.fini*)
    } > rom

    .rodata :
    {
        *(.rodata .rodata.*)
    } > rom

    .data :
    {
        *(.data .data.*)
    } > rom

    /* BSS must live in writable chip RAM, never in ROM. */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > ram

    . = ALIGN(2);
    _end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note*)
        *(.note.GNU-stack)
        *(.ARM.exidx*)
    }
}
