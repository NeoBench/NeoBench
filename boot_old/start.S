cpu 68020

xdef _start
xref kmain

_start:
    jsr kmain

.loop:
    bra .loop
