; roms/boot16.asm — 16-bit real-mode MBR (512 bytes)

[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Save the BIOS drive number (in DL) so we can reuse it
    mov [BOOT_DRIVE], dl

    ; ---- print "Loading stage 2..." via BIOS INT 10h ----
    mov si, msg
.print:
    lodsb               ; AL = [SI++]
    or  al, al
    jz  .done_print
    mov ah, 0x0E        ; teletype
    mov bh, 0x00        ; page 0
    mov bl, 0x07        ; light grey
    int 0x10
    jmp .print
.done_print:

    ; ---- RESET the floppy controller (INT 13h, AH=0) ----
    mov dl, [BOOT_DRIVE]
    mov ah, 0x00
    int 0x13
    jc  .disk_error     ; if reset fails

    ; ---- READ 1 sector (stage2) at CHS=0/0/2 into ES:BX=0x1000:0x0000 ----
    mov ah, 0x02        ; READ SECTORS
    mov al, 1           ; count = 1
    mov ch, 0           ; cyl 0
    mov cl, 2           ; sector 2
    mov dh, 0           ; head 0
    mov dl, [BOOT_DRIVE]
    mov bx, 0x0000
    mov ax, 0x1000      ; segment 0x1000
    mov es, ax
    int 0x13
    jc  .disk_error     ; if read fails

    ; ---- far-jump into your 64-bit code now at 0x1000:0x0000 ----
    jmp 0x1000:0x0000

.disk_error:
    mov si, err_msg
.print_err:
    lodsb
    or  al, al
    jz  .hang
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x04        ; red
    int 0x10
    jmp .print_err

.hang:
    hlt
    jmp .hang

; ---- data ----
msg        db "Loading stage 2...", 0
err_msg    db "Disk error!",        0
BOOT_DRIVE db 0    ; will be written once at start

; ---- padding & signature ----
times 510-($-$$) db 0
dw 0xAA55
