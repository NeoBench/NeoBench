.PHONY: all clean

all:
	$(MAKE) -C libs/libnbfs
	$(MAKE) -C tools/nbfs/mkfs
	$(MAKE) -C loader
	$(MAKE) -C kernel

clean:
	$(MAKE) -C libs/libnbfs clean
	$(MAKE) -C tools/nbfs/mkfs clean
	$(MAKE) -C loader clean
	$(MAKE) -C kernel clean
