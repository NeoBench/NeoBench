; NeoBoot stage2.asm — 16-bit real mode → 32-bit protected mode loader
BITS 16
ORG 0x1000

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Show stage2 message
    mov si, msg
    call print_string

    ; Wait for keypress
    mov ah, 0x00
    int 0x16

    ; Load kernel (assumes kernel starts at LBA 6)
    mov ah, 0x02         ; BIOS read sectors
    mov al, 4            ; number of sectors to read
    mov ch, 0            ; cylinder
    mov cl, 6            ; sector (LBA 6)
    mov dh, 0            ; head
    mov dl, 0x80         ; first HDD
    mov bx, 0x0000       ; load segment 0x0000:0x0000 = physical 0x00000
    mov es, bx
    mov bx, 0x0000       ; offset
    int 0x13

    ; Load rest of kernel at 0x100000
    ; BIOS doesn't support 1MB+ memory read in real mode
    ; We'll use protected mode to jump there.

    ; Setup GDT
    cli
    lgdt [gdt_descriptor]

    ; Enable protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to 32-bit code
    jmp CODE_SEG:init_pm

; ----------------------------------------
; 32-bit protected mode section
BITS 32

init_pm:
    ; Setup segment registers
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FB00

    ; Print to VGA text mode
    mov edi, 0xB8000
    mov esi, kernel_msg

.print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0F
    stosw
    jmp .print

.done:
    ; Jump to loaded kernel (physical 0x00100000)
    jmp 0x00100000

; ----------------------------------------
; 16-bit real mode section
BITS 16

print_string:
    mov ah, 0x0E
.next:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .next
.done:
    ret

; ----------------------------------------
; GDT Setup

gdt_start:
    dq 0x0000000000000000         ; null descriptor
    dq 0x00CF9A000000FFFF         ; code segment
    dq 0x00CF92000000FFFF         ; data segment

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

gdt_end:

CODE_SEG equ 0x08
DATA_SEG equ 0x10

msg db "NeoBoot Stage2...", 0
kernel_msg db "Entered Protected Mode", 0
