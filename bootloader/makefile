# bootloader/Makefile

AS=nasm
ASFLAGS=-f bin

KERNEL_AS=nasm
KERNEL_ASFLAGS=-f bin

BUILD_DIR=../build

all: $(BUILD_DIR)/neoboot.img

$(BUILD_DIR)/bootloader.bin: src/main.asm
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/stage2.bin: src/stage2.asm
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/kernel.bin: ../kernel/kernel_stub.asm
	$(KERNEL_AS) $(KERNEL_ASFLAGS) $< -o $@

$(BUILD_DIR)/neoboot.img: $(BUILD_DIR)/bootloader.bin $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/kernel.bin
	cat $(BUILD_DIR)/bootloader.bin $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/kernel.bin > $@

clean:
	rm -f $(BUILD_DIR)/*.bin
	rm -f $(BUILD_DIR)/neoboot.img
