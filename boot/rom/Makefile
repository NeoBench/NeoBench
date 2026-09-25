CROSS   := m68k-linux-gnu-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

export PATH            := $(HOME)/.local/m68k/bin:$(PATH)
export LD_LIBRARY_PATH := $(HOME)/.local/m68k/lib/x86_64-linux-gnu

TARGET  := neobench.rom
ELFS    := neobench.elf

COMMON_CFLAGS := \
    -m68000 \
    -Os \
    -ffreestanding \
    -fno-builtin \
    -fno-stack-protector \
    -fno-zero-initialized-in-bss \
    -fno-common \
    -Wall \
    -Wextra

KERNEL_DIR := ../../kernel

OBJS := \
    boot.o \
    amiga.o \
    string.o \
    $(KERNEL_DIR)/init/kernel_main.o \
    $(KERNEL_DIR)/init/banner.o \
    $(KERNEL_DIR)/init/console.o

.PHONY: all clean

all: $(TARGET)

%.o: %.c
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

$(KERNEL_DIR)/init/%.o: $(KERNEL_DIR)/init/%.c
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

$(ELFS): $(OBJS) rom.ld
	$(LD) -T rom.ld -nostdlib -o $@ $(OBJS)
	$(CROSS)size $@

$(TARGET): $(ELFS)
	$(OBJCOPY) -O binary -j .bootvec -j .text -j .rodata -j .data $(ELFS) $@
	truncate -s 524288 $@

clean:
	rm -f $(OBJS) $(ELFS) $(TARGET)