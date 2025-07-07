ENTRY(_start)

SECTIONS
{
  . = 0x00000000;

  .text : {
    *(.text*)
    *(.rodata*)
  }

  .data : {
    *(.data*)
  }

  .bss : {
    *(.bss*)
    *(COMMON)
  }

  /DISCARD/ : {
    *(.eh_frame)
  }
}
