OUTPUT_FORMAT(elf32-m68k)
OUTPUT_ARCH(m68k)
ENTRY(chain_entry)

MEMORY
{
    /*
     * Nominal link address for the chainloaded image.
     *
     * Nothing has to actually exist here.  LoadSeg() copies the hunk
     * wherever AllocMem() put it and adds (load - link) to every absolute
     * reference, so this address only has to leave room for the delta to
     * run in either direction without any reference underflowing past
     * zero.  All Amiga RAM -- chip and fast -- sits below $01000000, so
     * linking above it keeps every fixup positive.
     */
    ram (rwx) : ORIGIN = 0x01000000, LENGTH = 0x00400000
}

SECTIONS
{
    /*
     * chain_entry has to be the first thing in .text: the loader jumps to
     * the first byte of the first hunk, and there is no entry table to
     * redirect it.
     */
    .text :
    {
        KEEP(*(.text.chain_entry))
        KEEP(*(.text._start))
        *(.text*)
        *(.init*)
        *(.fini*)
    } > ram

    .rodata :
    {
        *(.rodata .rodata.*)
    } > ram

    .data :
    {
        *(.data .data.*)
    } > ram

    /*
     * The exception vector table.  Under a reset boot it is copied to
     * address 0 straight out of ROM; here it travels inside the hunk and
     * chain_entry writes it over AmigaOS's table once it owns the CPU.
     * It belongs inside the image (rather than in .bss) because it holds
     * absolute pointers that need the same relocation delta as the code
     * they point at.
     */
    .bootvec :
    {
        KEEP(*(.bootvec))
    } > ram

    /* Mutable globals.  Keeping them in .bss and not .data is what makes
     * them writable: .data would be fine here too, but the reset build
     * (rom.ld) maps .data into write-protected ROM, and one set of
     * sources has to serve both links. */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > ram

    . = ALIGN(4);
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
