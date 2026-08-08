CC ?= gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude

TOOLS = \
mkfs.nbfs \
fsck.nbfs \
mount.nbfs \
umount.nbfs \
dump.nbfs \
debug.nbfs \
label.nbfs \
tune.nbfs \
bench.nbfs \
stat.nbfs

all: $(TOOLS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TOOLS)
