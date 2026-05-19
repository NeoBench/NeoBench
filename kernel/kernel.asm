	idnt	"kernel_minimal.c"
	opt o+,ol+,op+,oc+,ot+,oj+,ob+,om+
	section	"CODE",code
	public	_kernel_main
	cnop	0,4
_kernel_main
	sub.w	#16,a7
	movem.l	l15,-(a7)
	move.l	#14676352,(0+l17,a7)
l3
	move.l	#0,(4+l17,a7)
	cmp.l	#4096,(4+l17,a7)
	bge	l3
l7
	move.l	(0+l17,a7),a0
	move.w	(6+l17,a7),(a0)
	move.l	#0,(10+l17,a7)
	cmp.l	#1000,(10+l17,a7)
	bge	l13
l11
	addq.l	#1,(10+l17,a7)
	cmp.l	#1000,(10+l17,a7)
	blt	l11
l13
	addq.l	#1,(4+l17,a7)
	cmp.l	#4096,(4+l17,a7)
	blt	l7
	bra	l3
l15	reg
l17	equ	0
	add.w	#16,a7
	rts
; stacksize=16
