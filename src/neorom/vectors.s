    .section .vectors, "a", @progbits
    .global _start

_start:
    .long 0x00000000        # Placeholder for checksum
    .long boot              # Entry point
    .long 0x00FF0000        # Initial Stack Pointer
