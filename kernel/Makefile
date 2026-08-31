CC=m68k-elf-gcc
OBJCOPY=m68k-elf-objcopy

CFLAGS = -m68030 \
         -ffreestanding \
         -fno-builtin \
         -fno-stack-protector \
         -Wall \
         -Wextra \
         -O2 \
         -Iinclude

ASFLAGS = -m68030

LDFLAGS = -m68030 \
          -nostdlib \
          -Wl,-T,arch/m68k/linker.ld

ROOTFS_DIR = ../rootfs/boot
ROOTFS_IMG = $(ROOTFS_DIR)/neobench-root.nbfs
ROOTFS_OBJ = $(ROOTFS_DIR)/rootfs.o

KERNEL_OBJS = \
        arch/m68k/start.o \
        init/kernel_main.o \
        init/console.o \
        init/banner.o \
        shell/neoshell.o \
        fs/block.o \
        fs/block/memdisk.o \
        fs/nbfs/nbfs.o \
        fs/vfs/vfs.o \
        fs/vfs/vnode.o \
        fs/vfs/file.o \
        fs/vfs/filesystem.o \
        fs/vfs/path.o \
        fs/vfs/dentry.o \
        fs/vfs/mount.o \
        lib/string.o \
        $(ROOTFS_OBJ)

all: kernel.elf

kernel.elf: $(KERNEL_OBJS)
	$(CC) $(LDFLAGS) \
		-o ../rootfs/boot/kernel.elf \
		$(KERNEL_OBJS) \
		-lgcc

#
# The NBFS root image is embedded into the kernel .data section so
# the boot path is independent of physical storage hardware.
#
# objcopy is run inside $(ROOTFS_DIR) so the generated symbols are
# _binary_neobench_root_nbfs_{start,end,size} (matching kernel_main.c).
#
$(ROOTFS_OBJ): $(ROOTFS_IMG)
	cd $(ROOTFS_DIR) && $(OBJCOPY) \
		-I binary \
		-O elf32-m68k \
		-B m68k \
		neobench-root.nbfs \
		rootfs.o

arch/m68k/%.o: arch/m68k/%.S
	$(CC) $(ASFLAGS) -c $< -o $@

init/%.o: init/%.c
	$(CC) $(CFLAGS) -c $< -o $@

shell/%.o: shell/%.c
	$(CC) $(CFLAGS) -c $< -o $@

fs/%.o: fs/%.c
	$(CC) $(CFLAGS) -c $< -o $@

lib/%.o: lib/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(KERNEL_OBJS)
	rm -f ../rootfs/boot/kernel.elf

.PHONY: all clean