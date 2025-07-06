; roms/boot16.asm — 16-bit real-mode stub
BITS 16
ORG 0x7C00

BOOT_DRIVE db 0

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; save drive number passed by BIOS
    mov [BOOT_DRIVE], dl

    ; print banner
    mov si, banner
.print:
    lodsb
    or al, al
    jz .after_banner
    mov ah, 0x0E
    mov bh, 0
    mov bl, 7
    int 0x10
    jmp .print
.after_banner:

    ; ES:BX = 0x1000:0
    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    ; CX = number of sectors to read (e.g. 64 sectors = 32 KiB)
    mov cx, 64

.read_loop:
    push cx
    mov ah, 0x02      ; read sectors
    mov al, 1         ; one sector per call
    mov ch, 0         ; cylinder 0
    mov cl, 2         ; starting with sector 2
    mov dh, 0         ; head 0
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .disk_error

    add bx, 512       ; advance BX by 512 bytes
    cmp bx, cx        ; not really needed, we use loop
    pop cx
    loop .read_loop

    ; far-jump into loaded code at 0x1000:0x0000
    jmp 0x1000:0x0000

.disk_error:
    mov si, err_msg
.err_print:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    mov bl, 4
    int 0x10
    jmp .err_print
.halt:
    hlt
    jmp .halt

;—— data ———————————————————————————————————————————————————
banner     db "Loading 64 sectors (32KiB) of NEOBENCH...",0
after_banner :
err_msg    db "Disk read error!",0

TIMES 510-($-$$) db 0
DW 0xAA55
