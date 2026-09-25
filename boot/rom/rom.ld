OUTPUT_FORMAT(elf32-m68k)
OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY
{
    /* 512 KiB kickstart region, A500/A1200-style. The processor reads its
     * reset vector table from the start of this image; code runs from $FC0000. */
    romvec (rx) : ORIGIN = 0x00F80000, LENGTH = 0x00040000
    rom    (rx) : ORIGIN = 0x00FC0000, LENGTH = 0x00040000

    /* Chip RAM scratch area, deliberately above the frame buffer
     * ($10000..$12800) and below the stack top ($7FFE0). */
    ram    (rw) : ORIGIN = 0x00020000, LENGTH = 0x0005E000
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
