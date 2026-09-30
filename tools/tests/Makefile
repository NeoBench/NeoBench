# Host tests: the parts of the ROM that are pure arithmetic and can be
# checked on the machine doing the building rather than in the emulator.
#
#   make -C tools/tests          build and run everything

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99

TESTS := test_zorro test_hid

all: run

test_zorro: test_zorro.c ../../boot/rom/zorro.c ../../boot/rom/zorro.h \
            ../../boot/rom/zz9000.h
	$(CC) $(CFLAGS) -o $@ test_zorro.c ../../boot/rom/zorro.c

# The HID parser and nothing else: it is the one part of the USB path
# with no hardware behind it anywhere (usb.c), so it is also the one part
# that can be driven from a table of reports here.
test_hid: test_hid.c ../../boot/rom/hid.c ../../boot/rom/hid.h \
          ../../boot/rom/kbd.h
	$(CC) $(CFLAGS) -o $@ test_hid.c ../../boot/rom/hid.c

run: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: all run clean
