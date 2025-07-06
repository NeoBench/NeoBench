/* roms/stage2.ld */
ENTRY(stage2_entry)
SECTIONS {
  . = 0x10000;            /* load at linear 0x1_0000 */
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss  : { *(.bss COMMON) }
  /DISCARD/ : { *(.eh_frame) }
}
