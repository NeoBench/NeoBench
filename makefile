# Paths
CC     := /home/adolf/x-tools/m68k-neobench-linux-gnu/bin/m68k-neobench-linux-gnu-gcc
LD     := /home/adolf/x-tools/m68k-neobench-linux-gnu/bin/m68k-neobench-linux-gnu-ld
OBJCOPY:= /home/adolf/x-tools/m68k-neobench-linux-gnu/bin/m68k-neobench-linux-gnu-objcopy

# Flags
CFLAGS := -ffreestanding -nostdlib -Wall -Wextra -O2 -m68000

# Files
BUILD_DIR      := build
SRC            := src/arch/m68k/rom.S
ROM_ELF        := $(BUILD_DIR)/neobench.elf
ROM_BIN        := $(BUILD_DIR)/neobench.rom
OBJ            := $(BUILD_DIR)/rom.o
RAW_LOGO       := $(BUILD_DIR)/neobench_logo.raw
RAW_SOUND      := $(BUILD_DIR)/boot_sound.raw
PNG_INPUT      := Modern_logo.png
WAV_INPUT      := Welcome\ to\ NeoBoot.wav

.PHONY: all clean run convert_logo convert_sound

all: $(ROM_BIN)

# Convert PNG to raw 320x256 8-bit planar
$(RAW_LOGO): $(PNG_INPUT)
	mkdir -p $(BUILD_DIR)
	magick convert $< -resize 320x256\! -colors 256 -depth 8 gray:$@

# Convert WAV to unsigned 8-bit mono PCM
$(RAW_SOUND): $(WAV_INPUT)
	mkdir -p $(BUILD_DIR)
	ffmpeg -y -i "$<" -ar 16000 -ac 1 -f u8 $@

# Compile assembly
$(OBJ): $(SRC) $(RAW_LOGO) $(RAW_SOUND)
	$(CC) $(CFLAGS) -c $< -o $@

# Link to ELF and convert to flat binary
$(ROM_BIN): $(OBJ)
	$(LD) -Ttext=0x000000 -N -o $(ROM_ELF) $(OBJ)
	$(OBJCOPY) -O binary $(ROM_ELF) $(ROM_BIN)
	truncate -s 524288 build/neobench.rom

clean:
	rm -rf $(BUILD_DIR)

