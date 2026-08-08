all:
	@echo "NeoBench unified build"

kernel:
	$(MAKE) -C ../kernel

bootloader:
	$(MAKE) -C ../boot/neoloader

libnbfs:
	$(MAKE) -C ../libs/libnbfs

userspace:
	$(MAKE) -C ../user

sdk:
	$(MAKE) -C ../sdk

clean:
	@echo "Cleaning build outputs"

.PHONY: all kernel bootloader libnbfs userspace sdk clean
