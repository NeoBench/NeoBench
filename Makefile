.PHONY: all clean iso

all:
	$(MAKE) -C libs/libnbfs
	$(MAKE) -C tools/nbfs/mkfs
	$(MAKE) -C loader
	$(MAKE) -C kernel

# The bootable installer disc: NeoBench's installer boot block at
# sector 0, the system tree as an NBFS volume in the payload, the hunk
# executable and the ROM beside it.  The disc is an artefact like the
# ROM is an artefact -- `make iso` rebuilds it, and images/ is not
# tracked.
iso:
	$(MAKE) -C boot/rom all
	$(MAKE) -C boot/rom chain
	rm -rf images/iso-root
	mkdir -p images/iso-root
	cp boot/rom/NeoBench images/iso-root/NEOBENCH.EXE
	cp boot/rom/neobench.rom images/iso-root/NEOBENCH.ROM
	cp system/Core/Docs/install.txt images/iso-root/INSTALL.TXT
	cp system/Core/Docs/readme.txt images/iso-root/README.TXT
	cp system/Core/Docs/changelog.txt images/iso-root/CHANGES.TXT
	python3 tools/mknbfs.py system images/iso-root/NBFS.IMG
	tools/nbfs/info/nbfs-info images/iso-root/NBFS.IMG | grep -q '^Config$$'
	python3 tools/mkiso.py images/iso-root images/NeoBench-0.1.7.iso

clean:
	$(MAKE) -C libs/libnbfs clean
	$(MAKE) -C tools/nbfs/mkfs clean
	$(MAKE) -C loader clean
	$(MAKE) -C kernel clean
