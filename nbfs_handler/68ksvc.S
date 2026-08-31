/* exec.library stubs for the m68k build.
 *
 * m68k-elf GCC passes every argument on the stack (first arg at 4(%sp),
 * pushed right-to-left at the call site), so these wrappers read args
 * from fixed stack offsets.  LVO offsets are literal displacements so the
 * backend never has to print a register-indexed jsr operand.
 */

        .text

/* void *nbh_alloc(void *sysbase, uint32_t size, uint32_t flags) */
        .globl nbh_alloc
        .type nbh_alloc, @function
nbh_alloc:
        move.l 4(%sp), %a6
        move.l 8(%sp), %d0
        move.l 12(%sp), %d1
        jsr -114(%a6)
        rts
        .size nbh_alloc, .-nbh_alloc

/* void nbh_free(void *sysbase, void *p, uint32_t size) */
        .globl nbh_free
        .type nbh_free, @function
nbh_free:
        move.l 4(%sp), %a6
        move.l 8(%sp), %a1
        move.l 12(%sp), %d0
        jsr -115(%a6)
        rts
        .size nbh_free, .-nbh_free

/* void *nbh_create_msgport(void *sysbase) */
        .globl nbh_create_msgport
        .type nbh_create_msgport, @function
nbh_create_msgport:
        move.l 4(%sp), %a6
        jsr -134(%a6)
        rts
        .size nbh_create_msgport, .-nbh_create_msgport

/* void nbh_delete_msgport(void *sysbase, void *port) */
        .globl nbh_delete_msgport
        .type nbh_delete_msgport, @function
nbh_delete_msgport:
        move.l 4(%sp), %a6
        move.l 8(%sp), %a1
        jsr -136(%a6)
        rts
        .size nbh_delete_msgport, .-nbh_delete_msgport

/* void nbh_waitport(void *sysbase, void *port) */
        .globl nbh_waitport
        .type nbh_waitport, @function
nbh_waitport:
        move.l 4(%sp), %a6
        move.l 8(%sp), %a0
        jsr -374(%a6)
        rts
        .size nbh_waitport, .-nbh_waitport

/* void *nbh_getmsg(void *sysbase, void *port) */
        .globl nbh_getmsg
        .type nbh_getmsg, @function
nbh_getmsg:
        move.l 4(%sp), %a6
        move.l 8(%sp), %a0
        jsr -320(%a6)
        rts
        .size nbh_getmsg, .-nbh_getmsg

/* void nbh_replymsg(void *sysbase, void *msg) */
        .globl nbh_replymsg
        .type nbh_replymsg, @function
nbh_replymsg:
        move.l 4(%sp), %a6
        move.l 8(%sp), %a1
        jsr -370(%a6)
        rts
        .size nbh_replymsg, .-nbh_replymsg

/* void *nbh_create_iorequest(void *sysbase, void *port, uint32_t size) */
        .globl nbh_create_iorequest
        .type nbh_create_iorequest, @function
nbh_create_iorequest:
        move.l 4(%sp), %a6
        move.l 8(%sp), %a0
        move.l 12(%sp), %d0
        jsr -342(%a6)
        rts
        .size nbh_create_iorequest, .-nbh_create_iorequest

/* void nbh_delete_iorequest(void *sysbase, void *req) */
        .globl nbh_delete_iorequest
        .type nbh_delete_iorequest, @function
nbh_delete_iorequest:
        move.l 4(%sp), %a6
        move.l 8(%sp), %a1
        jsr -345(%a6)
        rts
        .size nbh_delete_iorequest, .-nbh_delete_iorequest

/* int32_t nbh_open_device(void *sysbase, const char *name, uint32_t unit,
 *                         void *ioreq, uint32_t flags) */
        .globl nbh_open_device
        .type nbh_open_device, @function
nbh_open_device:
        move.l 4(%sp), %a6
        move.l 8(%sp), %a0
        move.l 12(%sp), %d0
        move.l 16(%sp), %a1
        move.l 20(%sp), %d1
        jsr -438(%a6)
        rts
        .size nbh_open_device, .-nbh_open_device

/* void nbh_doio(void *sysbase, void *ioreq) */
        .globl nbh_doio
        .type nbh_doio, @function
nbh_doio:
        move.l 4(%sp), %a6
        move.l 8(%sp), %a1
        jsr -402(%a6)
        rts
        .size nbh_doio, .-nbh_doio

        .end