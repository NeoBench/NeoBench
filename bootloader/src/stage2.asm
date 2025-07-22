[org 0x7E00]
bits 16

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov si, msg_stage2
    call print_string

    ; Detect memory via E820
    mov di, 0x9000        ; ES:DI points to destination for E820 entries
    xor ebx, ebx          ; continuation value
    xor bp, bp            ; count of entries

get_next_entry:
    mov eax, 0xE820
    mov edx, 0x534D4150   ; 'SMAP'
    mov ecx, 24           ; size of buffer
    mov es, di
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done

    add di, 24            ; next struct
    inc bp                ; count++
    test ebx, ebx
    jnz get_next_entry

.done:
    ; Save count
    mov word [0x8FFC], bp

    ; Load kernel into memory (already done by stage1 or BIOS for simplicity)
    ; Jump to 32-bit protected mode
    call switch_to_pm

    ; in pm now
    [bits 32]
    ; Jump to Rust kernel, pass memory map pointer in ESI
    mov esi, 0x00009000
    jmp 0x00100000

; -- VGA print helper
[bits 16]
print_string:
    pusha
.print_char:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp .print_char
.done:
    popa
    ret

msg_stage2 db "NeoBoot stage2... detecting RAM...", 0

; -- switch_to_pm would set up GDT and CR0 (already in your stage2)

; === GDT Setup and Protected Mode Switch ===
gdt_start:
gdt_null:
    dq 0x0000000000000000         ; null descriptor
gdt_code:
    dq 0x00CF9A000000FFFF         ; code segment: base=0, limit=4GB, exec/read
gdt_data:
    dq 0x00CF92000000FFFF         ; data segment: base=0, limit=4GB, read/write
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1    ; limit
    dd gdt_start                  ; base

; Actual routine to enable protected mode
switch_to_pm:
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:pm_start             ; far jump to flush prefetch queue

[bits 32]
pm_start:
    mov ax, 0x10                  ; data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; return from procedure now in 32-bit mode
    ret
