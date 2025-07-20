; bootloader/src/main.asm

BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; load 2nd stage to 0x1000
    mov si, msg
    call print_string

    mov ah, 0x02         ; BIOS read sector
    mov al, 4            ; read 4 sectors
    mov ch, 0            ; cylinder
    mov cl, 2            ; sector
    mov dh, 0            ; head
    mov dl, 0x80         ; first hard disk
    mov bx, 0x1000       ; load address
    int 0x13

    jmp 0x0000:0x1000    ; jump to loaded stage2 or kernel

print_string:
    mov ah, 0x0E
.next:
    lodsb
    or al, al
    jz .done
    int 0x10
    jmp .next
.done:
    ret

msg db "NeoBoot Loader", 0

; Boot signature
times 510 - ($ - $$) db 0
dw 0xAA55
