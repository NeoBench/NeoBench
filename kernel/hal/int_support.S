    SECTION .text,code

    XDEF _intr_disable
    XDEF _intr_enable
    XDEF _intr_disable_save
    XDEF _intr_restore

_intr_disable:
    or.w    #$0700,sr
    rts

_intr_enable:
    and.w   #$F8FF,sr
    rts

_intr_disable_save:
    move.w  sr,d0
    or.w    #$0700,sr
    rts

_intr_restore:
    move.w  4(sp),sr
    rts
