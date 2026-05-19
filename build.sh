#!/bin/bash

set -e

arm-none-eabi-gcc -ffreestanding -c stage1.S -o stage1.o
arm-none-eabi-gcc -ffreestanding -c loader.c -o loader.o
arm-none-eabi-ld -T linker.ld stage1.o loader.o -o neoloader.elf

arm-none-eabi-objcopy -O binary neoloader.elf neoloader.bin

echo "NeoLoader built: neoloader.bin"
