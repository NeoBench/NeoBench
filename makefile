ROM_NAME = build/neobench.rom
ELF_NAME = build/neobench.elf
OBJS = build/rom.o

all: $(ROM_NAME)

$(ROM_NAME): $(OBJS)
	$(CCPREFIX)ld -Ttext=0x000000 -N -o $(ELF_NAME) $(OBJS)
	$(CCPREFIX)objcopy -O binary $(ELF_NAME) $(ROM_NAME)
	dd if=/dev/zero bs=1k count=512 >> $(ROM_NAME)

build/rom.o: src/arch/m68k/rom.S
	mkdir -p build
	$(CCPREFIX)gcc -ffreestanding -nostdlib -Wall -Wextra -m68000 -O2 -c $< -o $@

clean:
	rm -rf build
