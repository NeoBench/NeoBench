CC=/home/adolf/x-tools/m68k-neobench-linux-gnu/bin/m68k-neobench-linux-gnu-gcc
LD=/home/adolf/x-tools/m68k-neobench-linux-gnu/bin/m68k-neobench-linux-gnu-ld
OBJCOPY=/home/adolf/x-tools/m68k-neobench-linux-gnu/bin/m68k-neobench-linux-gnu-objcopy

CFLAGS=-ffreestanding -nostdlib -Wall -Wextra -O2 -m68000

all: build/neobench.rom

build/neobench.rom: build/rom.o
	$(LD) -Ttext=0x000000 -N -o build/neobench.elf build/rom.o
	$(OBJCOPY) -O binary build/neobench.elf build/neobench.rom
	dd if=/dev/zero bs=1k count=512 >> build/neobench.rom

build/rom.o: src/arch/m68k/rom.S build/neobench_logo.raw build/boot_sound.raw
	$(CC) $(CFLAGS) -c $< -o $@

build/neobench_logo.raw: Modern_logo.png
	mkdir -p build
	magick convert $< -resize 320x256\! -colors 256 -depth 8 gray:build/neobench_logo.raw

build/boot_sound.raw: Welcome\ to\ NeoBoot.wav
	mkdir -p build
	ffmpeg -y -i "$<" -ar 16000 -ac 1 -f u8 build/boot_sound.raw

clean:
	rm -rf build
