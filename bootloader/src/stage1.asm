; bootloader/src/stage1.asm
; NeoBoot Stage 1 (MBR) - loads stage2 (sectors 2..(2+N-1)) to 0x0000:0x0600 and jumps there.

BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; simple stack inside boot sector area (OK short term)

    mov si, msg
    call print_string

    ; BIOS disk number in DL (already), keep it
    mov bx, 0x0600          ; load address offset for stage2 (segment still 0000)
    mov dh, 0               ; head
    mov ch, 0               ; cylinder
    mov cl, 2               ; starting sector (sector 1 is this MBR)
    mov al, 4               ; sectors to read (adjust when stage2 larger)
    mov ah, 0x02            ; INT 13h read
    int 0x13
    jc disk_error

    jmp 0x0000:0x0600       ; jump to stage2

disk_error:
    mov si, emsg
    call print_string
    hlt
    jmp $

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

msg  db "NeoBoot Stage1...",0
emsg db "Disk Error",0

times 510 - ($ - $$) db 0
dw 0xAA55
