# Host tests: the parts of the ROM that are pure arithmetic and can be
# checked on the machine doing the building rather than in the emulator.
#
#   make -C tools/tests          build and run everything

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99

TESTS := test_zorro test_hid test_pdf

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

# The DEFLATE decoder and the PDF reader: both are pure byte shuffling
# with no hardware and no caller to stand in for, and both are driven from
# data zlib and a hand-built document produced -- which is the only way to
# know the reader still reads rather than that it still compiles.
test_pdf: test_pdf.c ../../boot/rom/inflate.c ../../boot/rom/inflate.h \
          ../../boot/rom/pdf.c ../../boot/rom/pdf.h
	$(CC) $(CFLAGS) -o $@ test_pdf.c ../../boot/rom/inflate.c ../../boot/rom/pdf.c

run: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: all run clean
