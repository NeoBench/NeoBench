###############################################################################
# NeoBench kernel NBFS root host tests
###############################################################################

TARGET := test_kernel_root

CC      ?= cc

###############################################################################
# Compiler Flags
###############################################################################

CFLAGS := \
    -std=c11 \
    -Wall \
    -Wextra \
    -O2 \
    -I../../include \
    -I../../../include \
    -I../../../shared/include

# Set SANITIZE=1 for an asan/ubsan build (for tackling the
# kernel-path test during development).
ifeq ($(SANITIZE),1)
CFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS = -fsanitize=address,undefined
endif

###############################################################################
# Kernel files compiled on the host
###############################################################################

KERNEL_SRC := \
    ../../nbfs/nbfs.c \
    ../../block.c \
    ../../block/memdisk.c \
    ../../vfs/vfs.c \
    ../../vfs/vnode.c \
    ../../vfs/path.c \
    ../../vfs/file.c \
    ../../vfs/filesystem.c \
    ../../vfs/mount.c \
    ../../vfs/dentry.c \
    ../../vfs/tests/console_stub.c

BUILD := build

KERNEL_OBJ := $(addprefix $(BUILD)/, \
    nbfs.o block.o memdisk.o vfs.o vnode.o path.o \
    file.o nbkmount.o filesystem.o dentry.o console_stub.o)
OBJ := $(KERNEL_OBJ) $(BUILD)/test_kernel_root.o

###############################################################################
# Default
###############################################################################

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o $@

$(BUILD)/nbkmount.o: ../../vfs/mount.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/nbfs.o: ../../nbfs/nbfs.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/block.o: ../../block.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: ../../block/%.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: ../../vfs/%.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: ../../vfs/tests/%.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

###############################################################################
# Test
###############################################################################

test: $(TARGET)
	./$(TARGET)

###############################################################################
# Clean
###############################################################################

clean:
	rm -f $(OBJ)
	rm -f $(TARGET)

.PHONY: all test clean