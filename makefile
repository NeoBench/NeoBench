TARGET = neobench-m68k
ELF = target/$(TARGET)/release/neobench
ROM = roms/neobench.rom

all: $(ROM)

$(ROM): $(ELF)
	mkdir -p roms
	m68k-amigaos-objcopy -O binary $(ELF) $(ROM)

$(ELF):
	cargo build --release --target $(TARGET)

clean:
	cargo clean
	rm -f $(ROM)
