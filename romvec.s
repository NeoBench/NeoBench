    .section .vectors, "a"
    .globl _start

    .org 0x000000
    .long 0x00FF0000        # Initial stack pointer (any safe RAM)
    .long _start            # Entry point (must match Rust _start fn)

    .section .text
