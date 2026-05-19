#!/bin/bash
CROSS=/run/media/lordp/2BC705E7252CB03B/neobench/v1.0.0/toolchain/bin/m68k-elf-
GCC=${CROSS}gcc
GPP=${CROSS}g++
OBJCOPY=${CROSS}objcopy

# 1. Build Formatter
g++ -O2 -Wall -std=c++17 -m32 -I. -Iinclude -Idrivers -Idrivers/chipset -Ikernel -include include/gui_bridge.h -o format format.cpp

# 2. Build Kernel
$GCC -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -c -o build/boot/vectors.o boot/vectors.S
$GCC -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -c -o build/boot/start.o boot/start.S
$GPP -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -D__KERNEL__ -Iinclude -Idrivers/chipset -I. -O2 -c -o build/kernel/kernel.o kernel/kernel.cpp
$GPP -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -D__KERNEL__ -Iinclude -Idrivers/chipset -I. -O2 -c -o build/kernel/globals.o kernel/globals.cpp

# Stubs
${CROSS}nm -u build/boot/vectors.o build/boot/start.o build/kernel/kernel.o build/kernel/globals.o | grep ' U ' | awk '{print $2}' | sort -u > undefs.txt
${CROSS}nm -g build/boot/start.o build/kernel/kernel.o build/kernel/globals.o | grep ' [TD] ' | awk '{print $3}' | sort -u > defs.txt
echo ".section .text" > stubs.S
while read sym; do
    if grep -q "^${sym}$" defs.txt; then continue; fi
    if [[ "$sym" == "_start" || "$sym" == "_vector_table" ]]; then continue; fi
    echo ".global $sym" >> stubs.S
    echo "$sym: rts" >> stubs.S
done < undefs.txt
$GCC -m68040 -c -o build/stubs.o stubs.S

# Link
cat <<LINKER > linker.boot.ld
ENTRY(_start)
SECTIONS
{
    . = 0x10000;
    .vectors : { *(.vectors) }
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}
LINKER
$GPP -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -D__KERNEL__ -Iinclude -Idrivers/chipset -I. -O2 -T linker.boot.ld -o build/neobench.elf build/boot/vectors.o build/boot/start.o build/kernel/kernel.o build/kernel/globals.o build/stubs.o -lgcc
$OBJCOPY -O binary build/neobench.elf build/neobench.bin

# 3. Build Bootblock
cat <<BOOTBLOCK > bootblock.S
    .section .text
    .global _start
_start:
    .ascii "DOS\0"
    .long 0
    .long 880

    /* GREEN - Loader active */
    move.l  #0xdff180, %a0
    move.w  #0x00f0, (%a0)

    movem.l %d0-%d7/%a0-%a6, -(%sp)
    
    /* Load Kernel from Sector 2018 (offset within partition) */
    /* Physical Sector = PartitionStart(2016) + 2 = 2018 */
    /* Offset = 2018 * 512 = 1033216 */
    lea     0x10000.l, %a0
    move.l  #1033216, 0x2c(%a1) 
    move.l  #262144, 0x24(%a1)  
    move.l  %a0, 0x20(%a1)
    move.w  #2, 0x1c(%a1)      
    
    move.l  4.w, %a6
    jsr     -456(%a6)          
    
    movem.l (%sp)+, %d0-%d7/%a0-%a6

    /* BLUE - Handover */
    move.l  #0xdff180, %a0
    move.w  #0x000f, (%a0)
    
    move.l  0x10004.l, %a0
    jmp     (%a0)
BOOTBLOCK
$GCC -m68040 -nostdlib -Wl,--section-start=.text=0 -o build/bootblock.elf bootblock.S
$OBJCOPY -O binary build/bootblock.elf build/bootblock.bin

# 4. Master Format
# Ensure Neo_DE.hdf is the target
./format Neo_DE.hdf build/neobench.bin build/bootblock.bin

echo "MASTER NEOFS DISK READY: Neo_DE.hdf"
