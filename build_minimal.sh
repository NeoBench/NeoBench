#!/bin/bash
CROSS=/run/media/lordp/2BC705E7252CB03B/neobench/v1.0.0/toolchain/bin/m68k-elf-
GCC=$CROSS"gcc"
GPP=$CROSS"g++"
OBJCOPY=$CROSS"objcopy"

# 1. Build Kernel
$GCC -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -c -o build/boot/vectors.o boot/vectors.S
$GCC -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -c -o build/boot/start.o boot/start.S
$GPP -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -D__KERNEL__ -Iinclude -Idrivers/chipset -I. -O2 -c -o build/kernel/kernel.o kernel/kernel.cpp

# Satisfy symbols with stubs
$CROSS"nm" -u build/boot/vectors.o build/boot/start.o build/kernel/kernel.o | grep ' U ' | awk '{print $2}' | sort -u > undefs.txt
echo ".section .text" > stubs.S
echo ".global _kernel_main" >> stubs.S
echo "_kernel_main:" >> stubs.S
echo "    rts" >> stubs.S
while read sym; do
    if [[ "$sym" == "_start" || "$sym" == "_vector_table" || "$sym" == "_kernel_main" || "$sym" == "_cpu_type" ]]; then
        continue
    fi
    echo ".global $sym" >> stubs.S
    echo "$sym:" >> stubs.S
    echo "    rts" >> stubs.S
done < undefs.txt
$GCC -m68040 -c -o build/stubs.o stubs.S

# Link kernel at 0x10000
cat <<LINKER > linker.boot.ld
ENTRY(_start)
SECTIONS
{
    . = 0x10000;
    .vectors : { *(.vectors) }
    .text.startup : { *(.text.startup) }
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}
LINKER
$GPP -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -D__KERNEL__ -Iinclude -Idrivers/chipset -I. -O2 -T linker.boot.ld -o build/neobench.elf build/boot/vectors.o build/boot/start.o build/kernel/kernel.o build/stubs.o -lgcc
$OBJCOPY -O binary build/neobench.elf build/neobench.bin

# 2. Build Bootblock (PFS3 version)
# We place the kernel at Sector 1010 of the partition (LowCyl=1, so physical Sector 2018)
cat <<BOOTBLOCK > bootblock.S
    .section .text
    .global _start
_start:
    .ascii "DOS\0"
    .long 0
    .long 880

    movem.l %d0-%d7/%a0-%a6, -(%sp)
    
    lea     0x10000.l, %a0
    
    /* A1 = IORequest */
    /* Physical offset = (LowCyl * CylSize + SectorInCyl) * 512 */
    /* Here CylSize=1008. LowCyl=1. Kernel at Sector 2 in Cyl 1. */
    /* Physical Sector = 1008 + 2 = 1010. */
    /* Offset = 1010 * 512 = 517120 */
    move.l  #517120, 0x2c(%a1) 
    move.l  #65536, 0x24(%a1)  /* 64KB */
    move.l  %a0, 0x20(%a1)
    move.w  #2, 0x1c(%a1)
    
    move.l  4.w, %a6
    jsr     -456(%a6)          /* DoIO */
    
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    move.w  #0x2700, %sr
    jmp     0x10000.l
BOOTBLOCK
$GCC -m68000 -nostdlib -Wl,--section-start=.text=0 -o build/bootblock.elf bootblock.S
$OBJCOPY -O binary build/bootblock.elf build/bootblock.bin
truncate -s 1024 build/bootblock.bin
python3 fix_bootblock_checksum.py build/bootblock.bin

# 3. Assemble PFS3 HDF
cp pfs3_skeleton.hdf test_pfs3.hdf
# Write Bootblock at physical Sector 1008 (Start of Cyl 1)
dd if=build/bootblock.bin of=test_pfs3.hdf conv=notrunc bs=512 seek=1008
# Write Kernel at physical Sector 1010
dd if=build/neobench.bin of=test_pfs3.hdf conv=notrunc bs=512 seek=1010

echo "PFS3 Skeleton HDF with NeoBench Loader Created: test_pfs3.hdf"
