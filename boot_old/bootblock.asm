        bra.s start

        dc.b 'DOS',0
        dc.l 0

start:
        move.l #$40000,sp

.loop:
        bra.s .loop
