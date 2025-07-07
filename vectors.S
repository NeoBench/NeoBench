    .section .vectors, "a"

    .long 0x00080000      /* Initial stack pointer (top of 512KB) */
    .long _start          /* Reset vector points to _start */
