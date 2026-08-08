#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating NeoBench Build System"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p build
mkdir -p build/config
mkdir -p build/scripts
mkdir -p build/toolchains
mkdir -p build/images
mkdir -p build/releases
mkdir -p build/logs
mkdir -p build/cache
mkdir -p build/docs

###############################################################################
# Configuration
###############################################################################

cat > build/config/build.conf <<'EOF'
ARCH=m68k
CPU=68060
BUILD=release
FILESYSTEM=NBFS
BOOTLOADER=NeoLoader
INIT=Dinit
EOF

###############################################################################
# Build scripts
###############################################################################

SCRIPTS=(
build.sh
clean.sh
rebuild.sh
install.sh
package.sh
release.sh
run-qemu.sh
run-fsuae.sh
run-amiberry.sh
check.sh
)

for f in "${SCRIPTS[@]}"; do
cat > "build/scripts/$f" <<EOF
#!/usr/bin/env bash
set -Eeuo pipefail

echo "$f not implemented yet."
EOF
chmod +x "build/scripts/$f"
done

###############################################################################
# Toolchain files
###############################################################################

touch build/toolchains/m68k-gcc.cmake
touch build/toolchains/m68k-clang.cmake

###############################################################################
# Documentation
###############################################################################

touch build/docs/BUILD.md
touch build/docs/RELEASE.md
touch build/docs/CROSS_COMPILE.md

###############################################################################
# Top-level Makefile
###############################################################################

cat > build/Makefile <<'EOF'
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
EOF

echo
echo "========================================"
echo " Build System Created"
echo "========================================"

find build | sort
