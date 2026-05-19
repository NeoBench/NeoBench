STACKSTART = 0x1000;
STACKLEN   = 0x1000;

___MAPHI = 0xf0;

MEMORY
{
 zero : org = 2 , len = 0xFD
 ram : org = 0x2001, len=0xfffa-0x2001
 stack : org = STACKSTART, len=STACKLEN

}

SECTIONS
{
  text : {*(text)} >ram
  .dtors : { *(.dtors) } > ram
  .ctors : { *(.ctors) } > ram
  rodata : {*(rodata)} >ram
  data: {*(data)} >ram
  init : {*(init)} >ram
  zpage (NOLOAD) : {*(zpage) *(zp1) *(zp2)} >zero
  bss (NOLOAD): {*(bss)} >ram

  __BB = ADDR(bss);
  __BE = ADDR(bss)+SIZEOF(bss);
  __SE = STACKSTART+STACKLEN;
  ___heap = ADDR(bss)+SIZEOF(bss);
  ___heapend = 0xfff0;
}
