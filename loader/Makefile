###############################################################################
# NeoLoader Makefile
###############################################################################

TARGET  := loader.elf

CC      := m68k-elf-gcc
LD      := m68k-elf-gcc

CFLAGS = \
    -m68060 \
    -O2 \
    -Wall \
    -Wextra \
    -ffreestanding \
    -fno-builtin \
    -fno-stack-protector \
    -I. \
    -Iinclude \
    -I../include \
    -I../shared/include \
    -I../shared/libc

LDFLAGS := \
    -T linker.ld \
    -nostdlib

SRCS := \
main.c \
boot/boot.c \
loader_context.c \
cache/cache.c \
cache/inode_cache.c \
fs/block.c \
fs/directory.c \
fs/disk.c \
fs/file.c \
fs/inode.c \
fs/nbfs.c \
elf/elf_loader.c \
elf/elf_validate.c \
elf/elf_segments.c \
elf/elf_jump.c \
../shared/libc/memory.c \
../shared/libc/string.c

OBJS := $(SRCS:.c=.o)

###############################################################################
# Rules
###############################################################################

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
	rm -f $(TARGET)

rebuild: clean all

.PHONY: all clean rebuild
