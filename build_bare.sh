#!/bin/bash
CROSS=/run/media/lordp/2BC705E7252CB03B/neobench/v1.0.0/toolchain/bin/m68k-elf-
GCC=${CROSS}gcc
GPP=${CROSS}g++
OBJCOPY=${CROSS}objcopy
TARGET_HDF="Neo_DE.hdf"

# 1. Build Host Tool
g++ -O2 -Wall -std=c++17 -m32 -I. -Iinclude -Idrivers -Idrivers/chipset -Ikernel -include include/gui_bridge.h -o format format.cpp

# 2. Build Kernel
mkdir -p build/boot build/kernel
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

# Link Kernel at 0x10000
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

# 3. Build Direct Bootblock (Sector 0)
cat <<BOOTBLOCK > bootblock.S
    .section .text
    .global _start
_start:
    .ascii "DOS\0"
    .long 0
    .long 880

    /* GREEN - Loader running */
    move.l  #0xdff180, %a0
    move.w  #0x00f0, (%a0)

    movem.l %d0-%d7/%a0-%a6, -(%sp)
    
    /* Load Kernel from Sector 2 (1KB offset) */
    /* Offset = 2 * 512 = 1024 */
    lea     0x10000.l, %a0
    move.l  #1024, 0x2c(%a1) 
    move.l  #131072, 0x24(%a1)  /* 128KB */
    move.l  %a0, 0x20(%a1)
    move.w  #2, 0x1c(%a1)      
    
    move.l  4.w, %a6
    jsr     -456(%a6)          
    
    movem.l (%sp)+, %d0-%d7/%a0-%a6

    /* BLUE - Jumping */
    move.l  #0xdff180, %a0
    move.w  #0x000f, (%a0)
    
    /* Jump to kernel reset PC (Entry point) */
    move.l  0x10004.l, %a0
    jmp     (%a0)
BOOTBLOCK
$GCC -m68040 -nostdlib -Wl,--section-start=.text=0 -o build/bootblock.elf bootblock.S
$OBJCOPY -O binary build/bootblock.elf build/bootblock.bin
truncate -s 1024 build/bootblock.bin
python3 fix_bootblock_checksum.py build/bootblock.bin

# 4. Wipe and Assemble
# Clear the first 1MB of the HDF to remove RDB/PFS3
dd if=/dev/zero of=$TARGET_HDF conv=notrunc bs=1M count=1

# Write Bootblock at Sector 0
dd if=build/bootblock.bin of=$TARGET_HDF conv=notrunc bs=512 seek=0
# Write Kernel at Sector 2
dd if=build/neobench.bin of=$TARGET_HDF conv=notrunc bs=512 seek=2

# 5. Format NeoFS
./format $TARGET_HDF NEOBENCH

echo "PURE NEOFS BARE-METAL DISK CREATED."
