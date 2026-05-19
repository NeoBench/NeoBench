/*=============================================================================
 * NeoBench Kernel - Linker Script
 * Target: Motorola 68030/040/060 bare-metal Amiga
 *
 * Memory Map:
 *   0x000000 - 0x0003FF : Exception Vector Table (1KB)
 *   0x000400 - 0x000FFF : Reserved (boot data)
 *   0x001000 - 0x0FFFFF : Kernel code + data + BSS (Chip RAM)
 *   0x100000 - 0x1FFFFF : Chip RAM heap (if 2MB Chip)
 *   0x200000 - ...      : Fast RAM (kernel heap, stacks, user space)
 *
 * For ROM builds, kernel is linked at 0xF80000 (512KB ROM) or 0xF00000 (1MB ROM)
 *=============================================================================*/

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

/* Default to RAM build. Override with LDFLAGS for ROM build:
   -Ttext=0xF80000 -defsym ROM_BUILD=1 */
__ram_build = DEFINED(ROM_BUILD) ? 0 : 1;
__rom_origin = DEFINED(ROM_BUILD) ? 0xF80000 : 0x000000;

MEMORY
{
    vectors (rwx) : ORIGIN = 0x000000, LENGTH = 1K
    chipram (rwx) : ORIGIN = 0x000400, LENGTH = 2M - 1K
    /* Fast RAM is configured at runtime by the memory manager */
}

SECTIONS
{
    /* Exception Vector Table - must be at address 0 */
    .vectors 0x000000 :
    {
        KEEP(*(.vectors))
    } > vectors

    /* Startup code immediately after vectors */
    .text.startup 0x000400 :
    {
        *(.text.startup)
    } > chipram

    /* Main kernel code */
    .text : ALIGN(4)
    {
        _text_start = .;
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
        _text_end = .;
    } > chipram

    /* Read-only data */
    .rodata : ALIGN(4)
    {
        _rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        *(.gnu.linkonce.r.*)

        /* Font data */
        *(.font)
        *(.font.*)

        /* Copper lists */
        *(.copperlist)
        *(.copperlist.*)

        _rodata_end = .;
    } > chipram

    /* C++ constructors (global object init) */
    .init_array : ALIGN(4)
    {
        _init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        _init_array_end = .;
    } > chipram

    /* C++ destructors */
    .fini_array : ALIGN(4)
    {
        _fini_array_start = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        _fini_array_end = .;
    } > chipram

    /* Initialized data */
    .data : ALIGN(4)
    {
        _data_start = .;
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d.*)
        _data_end = .;
    } > chipram

    /* Uninitialized data (cleared to zero by startup code) */
    .bss (NOLOAD) : ALIGN(4)
    {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        _bss_end = .;
    } > chipram

    /* Chip RAM heap starts after BSS */
    .chip_heap (NOLOAD) : ALIGN(4096)
    {
        _chip_heap_start = .;
        /* We'll just define the start here, and length at runtime */
    } > chipram

    /* Kernel end marker */
    _kernel_end = .;
    _kernel_size = _kernel_end - 0x000000;

    /* Symbols for startup code */
    PROVIDE(_stack_top = ORIGIN(chipram) + LENGTH(chipram));
    _chip_heap_end = ORIGIN(chipram) + LENGTH(chipram);

    /* Discard unwanted sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
        *(.debug_*)
        *(.ARM.*)
    }
}

/* Assertions */
ASSERT(_kernel_size < 512K, "Kernel exceeds 512KB - won't fit in standard ROM!")
ASSERT(_bss_end - _bss_start < 256K, "BSS section exceeds 256KB!")
