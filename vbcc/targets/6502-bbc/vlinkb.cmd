HIMEM = 0x7C00 /*0x8000*/ ;
STACKSTART = 0x7B00 /*0x7F00*/ ;
OSHWM = 0x1900;

PAGESIZE = 0x4000;
PAGEADDR = 0x8000;

MEMORY
{
 zero : org = 0 , len = 0x90
 ram : org = OSHWM, len = (STACKSTART-OSHWM)

 /* just to force some gaps and make vlink create separate files */

 dummy0: org = 0x20000, len = 0x10000
 dummy1: org = 0x40000, len = 0x10000
 dummy2: org = 0x60000, len = 0x10000
 dummy3: org = 0x80000, len = 0x10000
 dummy4: org = 0xa0000, len = 0x10000
 dummy5: org = 0xc0000, len = 0x10000
 dummy6: org = 0xe0000, len = 0x10000
 dummy7: org = 0x100000, len = 0x10000
 dummy8: org = 0x120000, len = 0x10000
 dummy9: org = 0x140000, len = 0x10000
 dummy10: org = 0x160000, len = 0x10000
 dummy11: org = 0x180000, len = 0x10000
 dummy12: org = 0x1a0000, len = 0x10000
 dummy13: org = 0x1c0000, len = 0x10000
 dummy14: org = 0x1e0000, len = 0x10000
 dummy15: org = 0x200000, len = 0x10000

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
 b11 : org = PAGEADDR, len = PAGESIZE
 b12 : org = PAGEADDR, len = PAGESIZE
 b13 : org = PAGEADDR, len = PAGESIZE
 b14 : org = PAGEADDR, len = PAGESIZE
 b15 : org = PAGEADDR, len = PAGESIZE
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

/*
  b0 : {.=PAGEADDR; *(text0) *(rodata0) *(data0) *(bss0)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b0 AT>dummy0
*/

  b1 : {.=PAGEADDR; *(text1) *(rodata1) *(data1) *(bss1)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b1 AT>dummy1

  b2 : {.=PAGEADDR; *(text2) *(rodata2) *(data2) *(bss2)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b2 AT>dummy2

  b3 : {.=PAGEADDR; *(text3) *(rodata3) *(data3) *(bss3)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b3 AT>dummy3
/*
  b4 : {.=PAGEADDR; *(text4) *(rodata4) *(data4) *(bss4)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b4 AT>dummy4

  b5 : {.=PAGEADDR; *(text5) *(rodata5) *(data5) *(bss5)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b5 AT>dummy5

  b6 : {.=PAGEADDR; *(text6) *(rodata6) *(data6) *(bss6)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b6 AT>dummy6

  b7 : {.=PAGEADDR; *(text7) *(rodata7) *(data7) *(bss7)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b7 AT>dummy7

  b8 : {.=PAGEADDR; *(text8) *(rodata8) *(data8) *(bss8)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b8 AT>dummy8

  b9 : {.=PAGEADDR; *(text9) *(rodata9) *(data9) *(bss9)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b9 AT>dummy9

  b10 : {.=PAGEADDR; *(text10) *(rodata10) *(data10) *(bss10)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b10 AT>dummy10

  b11 : {.=PAGEADDR; *(text11) *(rodata11) *(data11) *(bss11)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b11 AT>dummy11

  b12 : {.=PAGEADDR; *(text12) *(rodata12) *(data12) *(bss12)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b12 AT>dummy12

  b13 : {.=PAGEADDR; *(text13) *(rodata13) *(data13) *(bss13)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b13 AT>dummy13

  b14 : {.=PAGEADDR; *(text14) *(rodata14) *(data14) *(bss14)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b14 AT>dummy14

  b15 : {.=PAGEADDR; *(text15) *(rodata15) *(data15) *(bss15)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b15 AT>dummy15
*/

  __BB = ADDR(bss);
  __BE = ADDR(bss)+SIZEOF(bss);
  ___heap = ADDR(bss)+SIZEOF(bss);
  ___heapend = STACKSTART-1;
}
