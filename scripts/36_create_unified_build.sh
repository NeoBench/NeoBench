#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating Unified Build System"
echo "========================================"

###############################################################################
# Build directories
###############################################################################

mkdir -p build
mkdir -p build/cache
mkdir -p build/logs
mkdir -p build/output
mkdir -p build/output/bin
mkdir -p build/output/lib
mkdir -p build/output/images
mkdir -p build/output/packages

###############################################################################
# Top-level Makefile
###############################################################################

cat > Makefile <<'EOF'
.PHONY: all clean kernel bootloader libs tools sdk user image

all: bootloader kernel libs tools sdk user image

bootloader:
	$(MAKE) -C boot/neoloader

kernel:
	$(MAKE) -C kernel

libs:
	$(MAKE) -C libs

tools:
	$(MAKE) -C tools

sdk:
	$(MAKE) -C sdk

user:
	$(MAKE) -C user

image:
	@echo "Creating NeoBench image..."

clean:
	@echo "Cleaning NeoBench..."
	find . -name '*.o' -delete
	find . -name '*.a' -delete
	find . -name '*.elf' -delete
	rm -rf build/output/*
EOF

###############################################################################
# Helper scripts
###############################################################################

cat > build/build.sh <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
make -j"$(nproc)"
EOF

cat > build/rebuild.sh <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
make clean
make -j"$(nproc)"
EOF

cat > build/image.sh <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
echo "NBFS image generation placeholder"
EOF

chmod +x build/build.sh
chmod +x build/rebuild.sh
chmod +x build/image.sh

echo
echo "Unified build system created."
