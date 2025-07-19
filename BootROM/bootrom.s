        section  ROM,code
        org      $000000

;---- Amiga ROM vector table (minimal) ----
        dc.l     _start         ; Reset vector
        dc.l     $00008000      ; Initial SSP
        ds.l     14             ; Unused vectors

;==== CODE ENTRY ====
_start:
        lea      $dff000,a6     ; CUSTOM chips base

;==== Set Palette ====
        lea      splash_palette,a0
        moveq    #31,d7
.palloop:
        move.w   (a0)+,$180(a6)
        dbra     d7,.palloop

;==== Setup bitplanes (4 planes: 16 colors, 320x256) ====
        lea      splash_bpl,a0      ; Start of bitplane data
        lea      $dff100,a4         ; BPL pointers
        lsl.w    #3,d7              ; 4 planes (0-3)
        move.w   d0,0(a4,d7.w)    ; BPLxPTH
        lsr.w    #3,d7
	swap     d0
        move.w   d0,4(a4,d7.w*8)    ; BPLxPTL
        lea      10240(a0),a0       ; Next plane (320*256/8)
        dbra     d7,.bplloop

;==== Display window, DMA, enable bitplanes ====
        move.w   #$2c81,$08e(a6)    ; DIWSTRT
        move.w   #$f4c1,$090(a6)    ; DIWSTOP
        move.w   #$0038,$092(a6)    ; DDFSTRT
        move.w   #$00d0,$094(a6)    ; DDFSTOP
        move.w   #$0240,$104(a6)    ; BPL1MOD, 0
        move.w   #$0240,$106(a6)    ; BPL2MOD, 0
        move.w   #$6200,$096(a6)    ; DMACON (enable bitplane + copper + sprite DMA)
        move.w   #$0204,$100(a6)    ; BPLCON0 (4 planes, color)

;==== Keyboard/CIAA: init ====
        lea      $bfe001,a1        ; CIAA base
        move.b   #$7f,(a1)         ; Set all lines input except PA7
        lea      $bfd000,a2        ; CIAB base (for future)

;==== Main Loop ====
mainloop:
        bsr      check_del
        bsr      poll_drivers      ; (stub, extend later!)
        bra      mainloop

;==== DEL Key: If pressed, call menu ====
check_del:
        lea      $bfe001,a1        ; CIAA
        move.b   (a1),d0           ; Read keyboard line
        not.b    d0
        andi.b   #$80,d0           ; DEL = 0x45 -> code 0x80 bit high when pressed
        beq      .no_del
        bsr      boot_menu
.no_del:
        rts

;==== Placeholder: Boot Device Scan ====
poll_drivers:
        ; call SCSI/IDE/USB/PCI/Zorro stubs here
        rts

;==== Placeholder: Boot Menu ====
boot_menu:
        ; As a demo: flash border color
        move.w   #$f00,$180(a6)
        bsr      delay
        move.w   #$0f0,$180(a6)
        bsr      delay
        rts

delay:
        move.l   #150000,d0
.dl:    subq.l   #1,d0
        bne      .dl
        rts

;==== Palette and Bitplanes Data (auto-generated, see Python script!) ====
splash_palette:
        incbin   "src/splash.pal"        ; 32x2 bytes
splash_bpl:
        incbin   "src/splash.bpl"        ; (or splash1.bpl, etc.)

;==== Pad to 512K ====
end_of_code:
    ds.b $80000-*
