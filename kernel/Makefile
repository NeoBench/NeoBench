CC=m68k-elf-gcc

CFLAGS = -m68060 \
         -ffreestanding \
         -fno-builtin \
         -fno-stack-protector \
         -Wall \
         -Wextra \
         -O2 \
         -Iinclude

ASFLAGS = -m68060

LDFLAGS = -m68060 \
          -nostdlib \
          -Wl,-T,arch/m68k/linker.ld

KERNEL_OBJS = \
        arch/m68k/start.o \
        init/kernel_main.o \
        init/console.o \
        init/banner.o \
        fs/block.o \
        fs/nbfs/nbfs.o \
        fs/vfs/vfs.o \
        fs/vfs/vnode.o \
        fs/vfs/file.o \
        fs/vfs/filesystem.o \
        fs/vfs/path.o \
        lib/string.o

all: kernel.elf

kernel.elf: $(KERNEL_OBJS)
	$(CC) $(LDFLAGS) \
		-o ../rootfs/boot/kernel.elf \
		$(KERNEL_OBJS) \
		-lgcc

arch/m68k/%.o: arch/m68k/%.S
	$(CC) $(ASFLAGS) -c $< -o $@

init/%.o: init/%.c
	$(CC) $(CFLAGS) -c $< -o $@

fs/%.o: fs/%.c
	$(CC) $(CFLAGS) -c $< -o $@

lib/%.o: lib/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(KERNEL_OBJS)
	rm -f ../rootfs/boot/kernel.elf
