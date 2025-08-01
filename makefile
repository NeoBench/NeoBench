BUILD_DIR = build

# Source files
SRC = src/neobench_desktop.c src/aga_chipset.c src/mc68060.c
LOGO_SOURCE = Modern_logo.png
AUDIO_SOURCE = Welcome to NeoBoot.wav

# Compiler and linker settings
CC = gcc
AS = as
LD = ld
OBJCOPY = objcopy

# Flags
CFLAGS = -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -c -I./include
ASFLAGS = --32 -W
LDFLAGS = -m elf_i386 -T linker.ld

# Main targets
all: $(BUILD_DIR)/neobench.rom

$(BUILD_DIR)/neobench.rom: $(BUILD_DIR)/neobench.elf
	@echo "Creating ROM image..."
	$(OBJCOPY) -O binary $< $@
	@echo "ROM created: $@"

$(BUILD_DIR)/neobench.elf: $(BUILD_DIR)/rom.o $(BUILD_DIR)/desktop.o $(BUILD_DIR)/aga.o $(BUILD_DIR)/m68060.o $(BUILD_DIR)/string.o $(BUILD_DIR)/stdlib.o
	@echo "Linking ELF file..."
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/rom.o: src/rom_x86.S $(BUILD_DIR)/neobench_logo.raw $(BUILD_DIR)/boot_sound.raw
	@echo "Assembling ROM object..."
	mkdir -p $(BUILD_DIR)
	$(AS) $(ASFLAGS) -o $@ src/rom_x86.S
	@echo "Checking ROM size..."
	@SIZE=$$(stat -c%s $(BUILD_DIR)/rom.o); \
	if [ $$SIZE -gt 524288 ]; then \
		echo "WARNING: ROM size exceeds 512KB limit ($$SIZE bytes)"; \
	else \
		echo "ROM size is within 512KB limit ($$SIZE bytes)"; \
	fi

$(BUILD_DIR)/desktop.o: src/neobench_desktop.c
	@echo "Compiling Vista-style desktop..."
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/aga.o: src/aga_chipset.c
	@echo "Compiling AGA chipset support..."
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/m68060.o: src/mc68060.c
	@echo "Compiling 68060 CPU support..."
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/string.o: src/string.c
	@echo "Compiling string library..."
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/stdlib.o: src/stdlib.c
	@echo "Compiling standard library..."
	$(CC) $(CFLAGS) -o $@ $<

# Logo processing (using placeholder)
$(BUILD_DIR)/neobench_logo.raw:
	@echo "Using placeholder logo file..."
	@mkdir -p $(BUILD_DIR)
	@if [ ! -f $(BUILD_DIR)/neobench_logo.raw ]; then \
		echo "NEOBENCH_LOGO_PLACEHOLDER" > $(BUILD_DIR)/neobench_logo.raw; \
	fi

# Audio processing (using placeholder)
$(BUILD_DIR)/boot_sound.raw:
	@echo "Using placeholder audio file..."
	@mkdir -p $(BUILD_DIR)
	@if [ ! -f $(BUILD_DIR)/boot_sound.raw ]; then \
		echo "NEOBENCH_BOOT_SOUND_PLACEHOLDER" > $(BUILD_DIR)/boot_sound.raw; \
	fi
	@echo "Audio placeholder created"

# Cleanup
clean:
	@echo "Cleaning build directory..."
	rm -rf $(BUILD_DIR)

.PHONY: all clean
