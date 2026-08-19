.section .text
.global neo_jump_to_kernel
.type neo_jump_to_kernel,@function

/*
 * Enter the NeoBench kernel.
 *
 * D0 = kernel entry address
 *
 * The loader has already established the machine state required
 * by the platform. We simply transfer control to the kernel.
 */

neo_jump_to_kernel:
    move.l  4(%sp),%a0
    jmp     (%a0)

.size neo_jump_to_kernel,.-neo_jump_to_kernel
