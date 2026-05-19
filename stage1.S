.global _start

.extern loader_main

_start:
    cli                 # disable interrupts

    # setup stack
    ldr sp, =stack_top

    # pass boot params (fake/firmware-dependent)
    mov r0, #0          # bootinfo ptr (placeholder)
    mov r1, #0          # memory map ptr (placeholder)

    bl loader_main

hang:
    b hang

.section .bss
.align 16
stack:
    .space 16384
stack_top:
