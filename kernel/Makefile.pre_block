CC=m68k-elf-gcc
LD=m68k-elf-ld

CFLAGS = -m68060 \
	-ffreestanding \
	-fno-builtin \
	-fno-stack-protector \
	-Wall \
	-Wextra \
	-O2 \
	-Iinclude

ASFLAGS = -m68060

KERNEL_OBJS = \
	arch/m68k/start.o \
	init/kernel_main.o \
	init/console.o \
	init/banner.o \
	fs/vfs/vfs.o \
	fs/vfs/vnode.o \
	fs/vfs/file.o \
	fs/vfs/filesystem.o \
	fs/vfs/path.o

all: kernel.elf

kernel.elf: $(KERNEL_OBJS)
	$(LD) \
		-T arch/m68k/linker.ld \
		-o $@ \
		$(KERNEL_OBJS)

arch/m68k/%.o: arch/m68k/%.S
	$(CC) $(ASFLAGS) -c $< -o $@

init/%.o: init/%.c
	$(CC) $(CFLAGS) -c $< -o $@

fs/vfs/%.o: fs/vfs/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(KERNEL_OBJS) kernel.elf

.PHONY: all clean
