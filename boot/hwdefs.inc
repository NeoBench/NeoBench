;=============================================================================
; NeoBench Kernel - Hardware Definitions (Assembly Include)
; Amiga Custom Chip Register Addresses
;=============================================================================

; CPU Type Constants
CPU_68030 EQU 30
CPU_68040 EQU 40
CPU_68060 EQU 60

; Hardware base
CUSTOM EQU $DFF000

; DMA/Interrupt registers
DMACON  EQU $096
INTENA  EQU $09A
INTREQ  EQU $09C
