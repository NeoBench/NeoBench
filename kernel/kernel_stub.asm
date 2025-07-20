; kernel/kernel_stub.asm - Minimal NeoBoot Kernel Stub
BITS 32
ORG 0x00100000         ; kernel is loaded here by stage2

_start:
    mov edi, 0xB8000   ; VGA text buffer
    mov esi, msg

.print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0A
    stosw
    jmp .print

.done:
    jmp $

msg db "NeoBoot Kernel Stub Running", 0
