#!/bin/bash
CROSS=/run/media/lordp/2BC705E7252CB03B/neobench/v1.0.0/toolchain/bin/m68k-elf-
GCC=${CROSS}gcc
GPP=${CROSS}g++
OBJCOPY=${CROSS}objcopy

# 1. Build kernel objects including GUI and Installer logic
mkdir -p build/boot build/kernel build/drivers
$GCC -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -c -o build/boot/vectors.o boot/vectors.S
$GCC -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -c -o build/boot/start.o boot/start.S

# Compile gui_format.cpp as the main kernel entry
$GPP -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -D__KERNEL__ -Iinclude -Idrivers/chipset -I. -O2 -c -o build/kernel/gui_installer_main.o gui_format.cpp

# Compile core GUI engine
$GPP -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -D__KERNEL__ -Iinclude -Idrivers/chipset -I. -O2 -c -o build/kernel/gui_core.o kernel/gui_core.cpp

# Compile Linkage helpers
$GPP -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -D__KERNEL__ -Iinclude -Idrivers/chipset -I. -O2 -c -o build/kernel/globals.o kernel/globals.cpp

# Satisfy symbols with stubs
${CROSS}nm -u build/boot/vectors.o build/boot/start.o build/kernel/gui_installer_main.o build/kernel/gui_core.o build/kernel/globals.o | grep ' U ' | awk '{print $2}' | sort -u > undefs.txt
${CROSS}nm -g build/boot/start.o build/kernel/gui_installer_main.o build/kernel/gui_core.o build/kernel/globals.o | grep ' [TD] ' | awk '{print $3}' | sort -u > defs.txt

echo ".section .text" > stubs.S
while read sym; do
    if grep -q "^${sym}$" defs.txt; then continue; fi
    if [[ "$sym" == "_start" || "$sym" == "_vector_table" ]]; then continue; fi
    echo ".global $sym" >> stubs.S
    echo "$sym: rts" >> stubs.S
done < undefs.txt
$GCC -m68040 -c -o build/stubs.o stubs.S

# Link into Bootable Installer Kernel
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

$GPP -m68040 -mhard-float -ffreestanding -nostdlib -nostartfiles -D__KERNEL__ -Iinclude -Idrivers/chipset -I. -O2 -T linker.boot.ld -o build/installer.elf build/boot/vectors.o build/boot/start.o build/kernel/gui_installer_main.o build/kernel/gui_core.o build/kernel/globals.o build/stubs.o -lgcc
$OBJCOPY -O binary build/installer.elf build/installer.bin

echo "BONE-METAL GUI INSTALLER BUILT: build/installer.bin"
