        ; VASM Motorola Syntax, M68020+
        ; Minimal working boot loader for QEMU -M virt

        ; Set origin to match QEMU virt base
        org     $08000000

        ; Main entry point
start:
        move.w  #$2700, sr             ; Enter supervisor mode
        lea     msg(pc), a0            ; Load message address
.loop:
        move.b  (a0)+, d0              ; Load byte
        beq     .done
        move.b  d0, $FF0002            ; Output byte (QEMU VirtIO)
        bra     .loop
.done:
        bra     .done

msg:
        dc.b    "NeoBench boot OK", 10, 0
