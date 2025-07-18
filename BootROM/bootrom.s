        section "ROM",code
        org     $0000

;--- Kickstart vector table (minimal, points to start)
        dc.l    _start                  ; 0x00: Initial PC (entry point)
        dc.l    $00008000               ; 0x04: Initial SP (arbitrary)
        ds.l    14                      ; 0x08: Unused/empty vectors

; ========== MAIN ENTRY ==========
_start:
        lea     $dff000,a6              ; A6 = CUSTOM (chipset regs)

        bsr     show_splash
        bsr     play_sound

.loop:                                  ; idle with flashing border
        move.w  #$0880,($180,a6)        ; blue border
        bsr     delay
        move.w  #$0f00,($180,a6)        ; red border
        bsr     delay
        bra     .loop

; ========== SPLASH BLIT ROUTINE ==========
show_splash:
        lea     $dff000,a6
        ; Bitplane pointers: $DFF0E0-$DFF0EE (BPL1PTH/PL, BPL2PTH/PL, ...)
        lea     splash_bpl,a0
        moveq   #0,d0
.setplanes:
        move.l  (a0)+,d1                ; address of bitplane data
        move.w  d1,$0E2(a6)             ; BPL1PTL, ...
        swap    d1
        move.w  d1,$0E0(a6)
        addq.l  #1,d0
        cmpi.l  #5,d0                   ; 5 bitplanes (32 color AGA)
        bls     .setplanes

        ; Set bitplane modulo (standard 320x256, no mod)
        move.w  #0,$108(a6)             ; BPL1MOD
        move.w  #0,$10A(a6)             ; BPL2MOD

        ; Set display window (standard PAL)
        move.w  #$2C81,$08E(a6)         ; DIWSTRT (h=0x2C, v=0x81)
        move.w  #$F4C1,$090(a6)         ; DIWSTOP (h=0xF4, v=0xC1)

        ; Set color palette
        lea     splash_pal,a0
        lea     $180(a6),a1             ; COLOR00 base
        moveq   #31,d0
.palette:
        move.w  (a0)+,(a1)
        addq.l  #2,a1
        dbra    d0,.palette

        rts

; ========== PLAY BOOT SOUND ==========
play_sound:
        lea     $dff000,a6
        lea     boot_sound,a0
        move.l  #boot_sound_end-boot_sound,d0  ; length in bytes
        lsr.l   #1,d0                          ; convert to words

        move.w  #0,$A8(a6)                     ; AUD0VOL = 0 (silence)
        move.w  #$0028,$A6(a6)                 ; AUD0PER = 8KHz (PAL: 3546895/8000/2 ≈ 221.7)
        move.l  a0,d1
        swap    d1
        move.w  d1,$0A0(a6)                    ; AUD0LCH
        swap    d1
        move.w  d1,$0A2(a6)                    ; AUD0LCL
        move.w  d0,$0A4(a6)                    ; AUD0LEN
        move.w  #64,$A8(a6)                    ; AUD0VOL = 64
        move.w  #$8200,$096(a6)                ; DMACON = AUD0EN + DMAEN

        ; crude wait for ~sound duration (not accurate)
        bsr     delay
        bsr     delay
        bsr     delay

        move.w  #0,$A8(a6)                     ; AUD0VOL = 0 (stop)
        move.w  #$0200,$09A(a6)                ; DMACON = AUD0EN off

        rts

; ========== DELAY LOOP ==========
delay:
        move.l  #120000,d0
.dloop:
        subq.l  #1,d0
        bne     .dloop
        rts

; ========== DATA/BITPLANE INCLUDE ==========
        align 4
splash_bpl:
        dc.l    splash_plane0, splash_plane1, splash_plane2, splash_plane3, splash_plane4

        align 4
splash_pal:
        incbin  "src/splash.pal"        ; 32x2 bytes (AGA/OCS/ECS format, optional)
                                        ; (or include palette inline here if you wish)

splash_plane0:
        incbin  "src/splash.bpl"        ; Each bitplane should be 10240 bytes (320*256/8)
splash_plane1:
        incbin  "src/splash1.bpl"
splash_plane2:
        incbin  "src/splash2.bpl"
splash_plane3:
        incbin  "src/splash3.bpl"
splash_plane4:
        incbin  "src/splash4.bpl"

boot_sound:
        incbin  "src/neoboot.raw"
boot_sound_end:

; ========== PAD ROM TO 512K ==========
        align 2
	ds.b $80000-(*-$$)
        end
