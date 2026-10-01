# NeoBench chainload bootblock.
#
# The block has to be a single contiguous 512-byte image with the ROM's
# header at the front, so it is linked at the address the ROM loads it to
# and flattened straight out of objcopy.  There is no second section and
# no relocation to carry: anything that needed relocating would not fit
# here in the first place.

CROSS   := m68k-linux-gnu-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

export PATH := $(HOME)/.local/m68k/bin:$(PATH)
export LD_LIBRARY_PATH := $(HOME)/.local/m68k/lib/x86_64-linux-gnu:$(LD_LIBRARY_PATH)

LOADADDR := 0x00070000
BLOCK    := bootblock.bin
IMAGE    ?= /tmp/opencode/nb.hdf

.PHONY: all image clean

all: $(BLOCK)

bootblock.o: bootblock.S
	$(CC) -m68000 -Os -ffreestanding -c $< -o $@

bootblock.elf: bootblock.o
	$(LD) -Ttext $(LOADADDR) -nostdlib -o $@ $<

$(BLOCK): bootblock.elf
	$(OBJCOPY) -O binary $< $@

image: $(BLOCK)
	python3 ../../tools/mkboot.py $(BLOCK) $(IMAGE)

clean:
	rm -f bootblock.o bootblock.elf $(BLOCK)
