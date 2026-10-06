CROSS   := m68k-linux-gnu-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

export PATH            := $(HOME)/.local/m68k/bin:$(PATH)
export LD_LIBRARY_PATH := $(HOME)/.local/m68k/lib/x86_64-linux-gnu

TARGET  := neobench.rom
ELFS    := neobench.elf

# Chainload flavour: same objects, different entry and link script, then
# handed to the Hunk converter because AmigaDOS will not load an ELF.
CHAIN_ELF  := neobench.chain.elf
CHAIN_EXE  := NeoBench
ELF2HUNK   := ../../tools/elf2hunk.py

COMMON_CFLAGS := \
    -m68000 \
    -Os \
    -ffreestanding \
    -fno-builtin \
    -fno-stack-protector \
    -fno-zero-initialized-in-bss \
    -fno-common \
    -Wall \
    -Wextra \
    -MMD \
    -MP

KERNEL_DIR := ../../kernel
USER_DIR   := ../../user/gui/desktop

# The Rust half: a static library cargo builds for the bare-metal m68k
# target and the link then takes beside the C like any other object.
# RUSTC_BOOTSTRAP lets the stable toolchain drive -Zbuild-std, which is
# what builds `core' for a tier-3 target that ships no prebuilt one, and
# RUSTFLAGS pins LLVM's CPU back from the target's own default (M68010)
# to the 68000 everything else in this ROM is compiled for.
RS_DIR  := ../../libs/nb_rs
RS_LIB  := $(RS_DIR)/target/m68k-unknown-none-elf/release/libnb_rs.a
CARGO   := $(HOME)/.cargo/bin/cargo

OBJS := \
    boot.o \
    amiga.o \
    string.o \
    fline.o \
    vbl.o \
    probe.o \
    ata.o \
    scsi.o \
    dev.o \
    iso9660.o \
    install.o \
    zorro.o \
    zz9000.o \
    gfx.o \
    lib32.o \
    pfs.o \
    pfs_data.o \
    inflate.o \
    pdf.o \
    prefs.o \
    pointer.o \
    audio.o \
    kbd.o \
    hid.o \
    usb.o \
    $(KERNEL_DIR)/init/kernel_main.o \
    $(KERNEL_DIR)/init/banner.o \
    $(KERNEL_DIR)/init/console.o \
    $(USER_DIR)/main.o \
    $(USER_DIR)/logo.o

# The preference store the kernel loads at boot, packed out of system/.
PFS_DATA := pfs_data.c
MKPFS    := ../../tools/mkpfs.py
PFS_SRCS := $(shell find ../../system -type f 2>/dev/null)

# The startup sound is built into the store before the store is packed,
# from the user's own recording in Core/Media/, so
# Core/Media/startup.snd never exists as an edited binary.
SND     := ../../system/Core/Media/startup.snd
MKSOUND := ../../tools/mksound.py
SNDWAV  := ../../system/Core/Media/neoboot.wav

.PHONY: all clean chain

all: $(TARGET)

chain: $(CHAIN_EXE)

%.o: %.c
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

$(SND): $(MKSOUND) $(SNDWAV)
	python3 $(MKSOUND) --wav $(SNDWAV) $(SND)

$(PFS_DATA): $(MKPFS) $(PFS_SRCS) $(SND)
	python3 $(MKPFS) ../../system $@

$(KERNEL_DIR)/init/%.o: $(KERNEL_DIR)/init/%.c
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

$(USER_DIR)/%.o: $(USER_DIR)/%.c
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

# Header dependencies (font8x8.h, amiga.h, ...); generated next to the objects.
-include $(OBJS:.o=.d)

$(RS_LIB): $(RS_DIR)/Cargo.toml $(RS_DIR)/Cargo.lock $(RS_DIR)/src/lib.rs
	@command -v $(CARGO) >/dev/null 2>&1 || \
	    { echo "rust: cargo is required and was not found at $(CARGO)"; exit 1; }
	cd $(RS_DIR) && RUSTC_BOOTSTRAP=1 RUSTC=$(HOME)/.cargo/bin/rustc \
	    RUSTFLAGS="-C target-cpu=M68000" \
	    $(CARGO) build --release --target m68k-unknown-none-elf \
	    -Zbuild-std=core,panic_abort

$(ELFS): $(OBJS) rom.ld $(RS_LIB)
	$(LD) -T rom.ld -nostdlib -o $@ $(OBJS) $(RS_LIB)
	$(CROSS)size $@

$(TARGET): $(ELFS)
	$(OBJCOPY) -O binary -j .bootvec -j .text -j .rodata -j .data $(ELFS) $@
	truncate -s 524288 $@

# The chainload link is the same code with chain.o in front of boot.o and
# ram.ld in place of rom.ld.  boot.o stays in the list because .bootvec
# and the fault stubs live there; its _start is simply never reached, the
# KEEP in ram.ld pinning chain_entry to offset 0 instead.
CHAIN_OBJS := $(OBJS) chain.o

$(CHAIN_ELF): $(CHAIN_OBJS) ram.ld $(RS_LIB)
	$(LD) -T ram.ld -nostdlib --emit-relocs -o $@ $(CHAIN_OBJS) $(RS_LIB)
	$(CROSS)size $@

$(CHAIN_EXE): $(CHAIN_ELF) $(ELF2HUNK)
	python3 $(ELF2HUNK) $< $@

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(PFS_DATA) $(SND) $(ELFS) $(TARGET) $(CHAIN_ELF) $(CHAIN_EXE)
	rm -rf $(RS_DIR)/target