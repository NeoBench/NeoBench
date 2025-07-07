ENTRY(_start)

MEMORY
{
  ROM (rx) : ORIGIN = 0x00000000, LENGTH = 512K
}

SECTIONS
{
  /* Vector table at 0x000000 */
  .vectors ORIGIN(ROM) :
  {
    KEEP(*(.vectors))
  } > ROM

  /* Code and data follow immediately */
  .text : ALIGN(4)
  {
    *(.text .text.*)
  } > ROM

  .rodata : ALIGN(4)
  {
    *(.rodata .rodata.*)
  } > ROM

  .data : ALIGN(4)
  {
    *(.data .data.*)
  } > ROM

  .bss (NOLOAD) : ALIGN(4)
  {
    *(.bss .bss.*)
    *(COMMON)
  } > ROM
}
