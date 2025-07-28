#define UART_TX (*(volatile unsigned char *)0x10000000)

void uart_putc(char c) {
    UART_TX = c;
}

int main() {
    uart_putc('N');
    uart_putc('E');
    uart_putc('O');
    uart_putc('\n');
    while (1) {}
    return 0;
}
