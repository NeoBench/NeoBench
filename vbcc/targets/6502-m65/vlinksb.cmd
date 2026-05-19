STACKSTART = 0x200;
STACKLEN   = 0x600;

PAGESIZE = 0x4000;
PAGEADDR = 0x8000;

___MAPHI = 0xb0;

MEMORY
{
 zero : org = 2 , len = 0xfd
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
 b11 : org = PAGEADDR, len = PAGESIZE
 b12 : org = PAGEADDR, len = PAGESIZE
 b13 : org = PAGEADDR, len = PAGESIZE
 b14 : org = PAGEADDR, len = PAGESIZE
 b15 : org = PAGEADDR, len = PAGESIZE
 b16 : org = PAGEADDR, len = PAGESIZE
 b17 : org = PAGEADDR, len = PAGESIZE
 b18 : org = PAGEADDR, len = PAGESIZE
 b19 : org = PAGEADDR, len = PAGESIZE
 b20 : org = PAGEADDR, len = PAGESIZE
 b21 : org = PAGEADDR, len = PAGESIZE

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

  b11: {.=PAGEADDR; *(text11) *(rodata11) *(data11) *(bss11)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b11 AT>banks

  b12: {.=PAGEADDR; *(text12) *(rodata12) *(data12) *(bss12)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b12 AT>banks

  b13: {.=PAGEADDR; *(text13) *(rodata13) *(data13) *(bss13)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b13 AT>banks

  b14: {.=PAGEADDR; *(text14) *(rodata14) *(data14) *(bss14)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b14 AT>banks

  b15: {.=PAGEADDR; *(text15) *(rodata15) *(data15) *(bss15)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b15 AT>banks

  b16: {.=PAGEADDR; *(text16) *(rodata16) *(data16) *(bss16)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b16 AT>banks

  b17: {.=PAGEADDR; *(text17) *(rodata17) *(data17) *(bss17)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b17 AT>banks

  b18: {.=PAGEADDR; *(text18) *(rodata18) *(data18) *(bss18)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b18 AT>banks

  b19: {.=PAGEADDR; *(text19) *(rodata19) *(data19) *(bss19)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b19 AT>banks

  b20: {.=PAGEADDR; *(text20) *(rodata20) *(data20) *(bss20)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b20 AT>banks

  b21: {.=PAGEADDR; *(text21) *(rodata21) *(data21) *(bss21)
       RESERVE(PAGEADDR+PAGESIZE-.);
      } >b21 AT>banks

  __BB = ADDR(bss);
  __BE = ADDR(bss)+SIZEOF(bss);
  __SE = STACKSTART+STACKLEN;
  ___heap = ADDR(bss)+SIZEOF(bss);
  ___heapend = 0x7fff;
}
