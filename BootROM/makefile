CC=m68k-amigaos-g++
CFLAGS=-noixemul -m68060 -O2 -nostdlib -nostartfiles -I/home/adolf/amiga-ndk/Include_H -Isrc

all: bootrom.512k

bootrom.elf: src/bootrom.cpp src/splash_planes.h
	$(CC) $(CFLAGS) $< -o $@

bootrom.bin: bootrom.elf
	m68k-amigaos-objcopy -O binary bootrom.elf bootrom.bin

bootrom.512k: bootrom.bin
	dd if=bootrom.bin of=bootrom.512k bs=512K conv=sync

clean:
	rm -f bootrom.elf bootrom.bin bootrom.512k
