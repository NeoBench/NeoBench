CC=/home/adolf/x-tools/m68k-neobench-linux-gnu/bin/m68k-neobench-linux-gnu-gcc
LD=/home/adolf/x-tools/m68k-neobench-linux-gnu/bin/m68k-neobench-linux-gnu-ld
OBJCOPY=/home/adolf/x-tools/m68k-neobench-linux-gnu/bin/m68k-neobench-linux-gnu-objcopy
CFLAGS=-ffreestanding -nostdlib -Wall -Wextra -O2 -m68060
LDFLAGS=-T linker.ld -N

SRC=src/arch/m68k/rom.S
BUILD_DIR=build

all: $(BUILD_DIR)/neobench.rom

$(BUILD_DIR)/neobench.rom: $(BUILD_DIR)/neobench.elf
	$(OBJCOPY) -O binary $< $@
	truncate -s 524288 $@

$(BUILD_DIR)/neobench.elf: $(BUILD_DIR)/rom.o
	mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/rom.o: $(SRC) $(BUILD_DIR)/neobench_logo.raw $(BUILD_DIR)/boot_sound.raw
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/neobench_logo.raw: Modern_logo.png
	mkdir -p $(BUILD_DIR)
	magick Modern_logo.png -resize 320x256\! -colors 2 -depth 1 gray:$(BUILD_DIR)/neobench_logo.raw

# Create boot sound - try multiple methods
$(BUILD_DIR)/boot_sound.raw:
	mkdir -p $(BUILD_DIR)
	@echo "Creating boot sound..."
	@python3 -c "\
import math, struct; \
sample_rate = 8000; \
duration = 0.5; \
frequency = 880; \
samples = int(sample_rate * duration); \
data = [int(127 + 100 * math.sin(2 * math.pi * frequency * i / sample_rate)) for i in range(samples)]; \
open('$(BUILD_DIR)/boot_sound.raw', 'wb').write(bytes(data))"
	@echo "Generated $$(wc -c < $(BUILD_DIR)/boot_sound.raw) bytes of audio data"

# Alternative target if you have a WAV file
$(BUILD_DIR)/boot_sound_from_wav.raw: Welcome\ to\ NeoBoot.wav
	mkdir -p $(BUILD_DIR)
	ffmpeg -y -i "Welcome to NeoBoot.wav" -ar 8000 -ac 1 -t 0.5 -f u8 $(BUILD_DIR)/boot_sound.raw

clean:
	rm -rf $(BUILD_DIR)

# Debug targets
debug-logo: $(BUILD_DIR)/neobench_logo.raw
	@echo "Logo file size: $$(wc -c < $(BUILD_DIR)/neobench_logo.raw) bytes"
	@echo "Expected size for 320x256x1bit: $$(( 320 * 256 / 8 )) bytes"

debug-sound: $(BUILD_DIR)/boot_sound.raw
	@echo "Sound file size: $$(wc -c < $(BUILD_DIR)/boot_sound.raw) bytes"
	@echo "Duration at 8kHz: $$(( $$(wc -c < $(BUILD_DIR)/boot_sound.raw) / 8000 )) seconds"

debug-rom: $(BUILD_DIR)/neobench.rom
	@echo "ROM file size: $$(wc -c < $(BUILD_DIR)/neobench.rom) bytes (should be 524288)"
	@echo "First 32 bytes:"
	@hexdump -C $(BUILD_DIR)/neobench.rom | head -3

# Help target
help:
	@echo "NeoBench ROM Build System"
	@echo "========================="
	@echo ""
	@echo "This appears to be an Amiga AGA system ROM based on the assembly code:"
	@echo "- Uses Amiga custom chip registers (0xdff096 = DMACON, etc.)"
	@echo "- 68060 processor target"
	@echo "- 320x256 1-bit logo display"
	@echo "- 8kHz mono audio playback"
	@echo ""
	@echo "Build Targets:"
	@echo "  all                    - Build complete ROM (default)"
	@echo "  build/neobench.rom     - Final 512KB ROM file"
	@echo "  build/neobench_logo.raw - Processed logo bitmap"
	@echo "  build/boot_sound.raw   - Boot sound audio data"
	@echo "  clean                  - Remove build directory"
	@echo ""
	@echo "Debug Targets:"
	@echo "  debug-logo            - Show logo file info"
	@echo "  debug-sound           - Show sound file info"
	@echo "  debug-rom             - Show ROM file info and hex dump"
	@echo ""
	@echo "Requirements:"
	@echo "  - m68k-neobench-linux-gnu toolchain"
	@echo "  - ImageMagick (magick command)"
	@echo "  - Python3 (for audio generation)"
	@echo "  - Optional: ffmpeg (for WAV file processing)"

.PHONY: all clean debug-logo debug-sound debug-rom help
