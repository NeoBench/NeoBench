# NeoBench FreeBSD stable/15 kernel integration helper
#
# The upstream FreeBSD source is intentionally kept outside the NeoBench tree.
# This target fetches a pinned stable/15 checkout and applies the NeoBench
# m68k/68060 machine configuration.

FREEBSD_REPO ?= https://github.com/freebsd/freebsd-src.git
FREEBSD_BRANCH ?= stable/15
FREEBSD_DIR ?= $(CURDIR)/freebsd-src

.PHONY: fetch configure clean

fetch:
	@if [ ! -d "$(FREEBSD_DIR)/.git" ]; then \
		git clone --branch $(FREEBSD_BRANCH) --single-branch $(FREEBSD_REPO) $(FREEBSD_DIR); \
	else \
		git -C $(FREEBSD_DIR) fetch origin $(FREEBSD_BRANCH); \
		git -C $(FREEBSD_DIR) checkout $(FREEBSD_BRANCH); \
		git -C $(FREEBSD_DIR) reset --hard origin/$(FREEBSD_BRANCH); \
	fi

configure: fetch
	@echo "NeoBench: FreeBSD $(FREEBSD_BRANCH) source is ready"
	@echo "Target: Amiga 68060 / m68k"
	@echo "Machine port: kernel/arch/m68k/neobench"

clean:
	rm -rf $(FREEBSD_DIR)
