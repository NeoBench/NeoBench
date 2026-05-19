.text

.ascii "DOS"
.byte 0

.long 0
.long 880

.globl _boot

_boot:
    moveq #0,%d0

loop:
    bra.s loop
