# Host tests: the parts of the ROM that are pure arithmetic and can be
# checked on the machine doing the building rather than in the emulator.
#
#   make -C tools/tests          build and run everything

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99

TESTS := test_zorro test_hid test_pdf test_iso9660

# The install disc the last test reads is built by the tools that build
# the real one: mknbfs.py packs fixtures/nbfs-tree into a volume, that
# volume is staged as NBFS.IMG beside the ISO tree's files, and mkiso.py
# presses the lot into build/fixture.iso.  Rebuilding it keeps the test
# honest about what a disc actually looks like on the wire.
FIXTURE := build/fixture.iso

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

$(FIXTURE): fixtures/iso-tree/INSTALL.TXT fixtures/iso-tree/DOCS/INNER.TXT \
            fixtures/nbfs-tree/hello.txt fixtures/nbfs-tree/sub/inner.txt \
            ../../tools/mkiso.py ../../tools/mknbfs.py
	rm -rf build
	mkdir -p build/iso/DOCS
	python3 ../../tools/mknbfs.py fixtures/nbfs-tree build/iso/NBFS.IMG --blocks 400
	cp fixtures/iso-tree/INSTALL.TXT build/iso/
	cp fixtures/iso-tree/DOCS/INNER.TXT build/iso/DOCS/
	python3 ../../tools/mkiso.py build/iso build/fixture.iso

# The reader, the installer and the device table, with the two fake
# devices the test registers standing in for the hardware (test file
# header has the whole picture).
test_iso9660: test_iso9660.c ../../boot/rom/iso9660.c \
              ../../boot/rom/iso9660.h ../../boot/rom/install.c \
              ../../boot/rom/install.h ../../boot/rom/dev.c \
              ../../boot/rom/dev.h ../../boot/rom/amiga.h $(FIXTURE)
	$(CC) $(CFLAGS) -o $@ test_iso9660.c ../../boot/rom/iso9660.c \
	    ../../boot/rom/install.c ../../boot/rom/dev.c

run: $(TESTS) $(FIXTURE)
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -f $(TESTS)
	rm -rf build

.PHONY: all run clean
