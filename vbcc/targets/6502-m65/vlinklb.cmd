STACKSTART = 0x200;
STACKLEN   = 0x600;

PAGESIZE = 0x8000;
PAGEADDR = 0x8000;

___MAPHI = 0xf0;

MEMORY
{
 zero : org = 2 , len = 0xfc
 ram : org = 0x1000, len=0x5800
 stack : org = STACKSTART, len=STACKLEN
 banks : org = 0x8000, len = 0x58000
 b0 : org = PAGEADDR, len = PAGESIZE
 b1 : org = PAGEADDR, len = PAGESIZE
 b2 : org = PAGEADDR, len = PAGESIZE
 b3 : org = PAGEADDR, len = PAGESIZE
 b4 : org = PAGEADDR, len = PAGESIZE
 b5 : org = PAGEADDR, len = PAGESIZE
 b6 : org = PAGEADDR, len = PAGESIZE
 b7 : org = PAGEADDR, len = PAGESIZE
 b8 : org = PAGEADDR, len = PAGESIZE
 b9 : org = PAGEADDR, len = PAGESIZE
 b10 : org = PAGEADDR, len = PAGESIZE
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


  b0: {.=PAGEADDR; *(text0) *(rodata0) *(data0) *(bss0)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b0 AT>banks

  b1: {.=PAGEADDR; *(text1) *(rodata1) *(data1) *(bss1)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b1 AT>banks

  b2: {.=PAGEADDR; *(text2) *(rodata2) *(data2) *(bss2)
       RESERVE(PAGEADDR+PAGESIZE/*-0x800*/-.);
       /*FILL8(1);
       RESERVE(0x800);
       FILL8(0);*/
      } >b2 AT>banks

  b3: {.=PAGEADDR; *(text3) *(rodata3) *(data3) *(bss3)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b3 AT>banks

  b4: {.=PAGEADDR; *(text4) *(rodata4) *(data4) *(bss4)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b4 AT>banks

  b5: {.=PAGEADDR; *(text5) *(rodata5) *(data5) *(bss5)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b5 AT>banks

  b6: {.=PAGEADDR; *(text6) *(rodata6) *(data6) *(bss6)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b6 AT>banks

  b7: {.=PAGEADDR; *(text7) *(rodata7) *(data7) *(bss7)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b7 AT>banks

  b8: {.=PAGEADDR; *(text8) *(rodata8) *(data8) *(bss8)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b8 AT>banks

  b9: {.=PAGEADDR; *(text9) *(rodata9) *(data9) *(bss9)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b9 AT>banks

  b10: {.=PAGEADDR; *(text10) *(rodata10) *(data10) *(bss10)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b10 AT>banks

  __BB = ADDR(bss);
  __BE = ADDR(bss)+SIZEOF(bss);
  __SE = STACKSTART+STACKLEN;
  ___heap = ADDR(bss)+SIZEOF(bss);
  ___heapend = 0x7fff;
}
