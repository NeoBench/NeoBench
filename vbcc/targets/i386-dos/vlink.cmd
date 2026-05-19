MEMORY
{
  ram : org = 0x100, len = 0xff00
}


SECTIONS
{
  .text : {*(.text)} >ram
  .dtors : { *(.dtors) } > ram
  .ctors : { *(.ctors) } > ram
  .rodata : {*(.rodata)} >ram
  .data: {*(.data)} >ram
  .bss (NOLOAD): {*(.bss COMMON)} >ram


  __BB = ADDR(.bss);
  __BE = ADDR(.bss)+SIZEOF(.bss);
  __SE = 0xffff;
  ___heap = ADDR(.bss)+SIZEOF(.bss);


}
