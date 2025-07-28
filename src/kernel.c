void uart_putc(char c) {
    asm volatile (
        "move.b %0, %%d0\n\t"
        "trap #15"             // example trap, won't work unless emulator or BIOS implements it
        :
        : "d"(c)
    );
}
