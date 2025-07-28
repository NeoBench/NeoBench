CC = /home/adolf/x-tools/m68k-neobench-linux-gnu/bin/m68k-neobench-linux-gnu-gcc
LD = /home/adolf/x-tools/m68k-neobench-linux-gnu/bin/m68k-neobench-linux-gnu-ld

CFLAGS = -ffreestanding -nostdlib -Wall -Wextra -m68060 -O2

BUILD_DIR = build
SRC_DIR = src

SOURCES = \
    $(SRC_DIR)/kernel.c \
    $(SRC_DIR)/arch/m68k/boot.s

OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
OBJECTS := $(OBJECTS:$(SRC_DIR)/%.s=$(BUILD_DIR)/%.o)

all: $(BUILD_DIR)/neobench.elf

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/neobench.elf: $(OBJECTS) link.ld
	$(LD) -Tlink.ld -nostdlib -o $@ $(OBJECTS)

run: all
	qemu-system-m68k -M virt -nographic -kernel $(BUILD_DIR)/neobench.elf -serial mon:stdio

clean:
	rm -rf $(BUILD_DIR)
