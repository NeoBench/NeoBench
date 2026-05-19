STACKLEN   = 0x200;
STACKSTART = 0xc000 - 0x200;

MEMORY
{
 zero : org = 2 , len = 0x8d
 ram : org = 0x1c01, len=0xbfff-0x1c01-STACKLEN
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
  bss (NOLOAD): {*(bss)} >ram
  /* data copy section + zpage save */

  zpage (NOLOAD) : {*(zpage) *(zp1) *(zp2)} >zero

  __BB = ADDR(bss);
  __BE = ADDR(bss)+SIZEOF(bss);
  __ZB = ADDR(zpage);
  __ZS = SIZEOF(zpage);
  __ZC = ADDR(bss)+SIZEOF(bss)+SIZEOF(data);
  __DB = ADDR(data);
  __DE = ADDR(data)+SIZEOF(data);
  __DC = ADDR(bss)+SIZEOF(bss);
  __SE = STACKSTART+STACKLEN;
  ___heap = ADDR(bss)+SIZEOF(bss)+SIZEOF(data)+SIZEOF(zpage);
  ___heapend = STACKSTART-1;
}
