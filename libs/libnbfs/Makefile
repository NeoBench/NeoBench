CC ?= gcc
AR ?= ar

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

TARGET := build/libnbfs.a

CFLAGS := \
	-Wall \
	-Wextra \
	-std=c11 \
	-O2 \
	-Iinclude \
	-I../../include \
	-I../../shared/include

LDFLAGS := -Lbuild
LDLIBS := -lnbfs

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p build
	$(AR) rcs $(TARGET) $(OBJ)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

tests:
	@for src in tests/*.c; do \
		if grep -Eq '^[[:space:]]*(int|void)[[:space:]]+main[[:space:]]*\(' "$$src"; then \
			bin="$${src%.c}"; \
			echo "BUILD: $$src -> $$bin"; \
			$(CC) $(CFLAGS) "$$src" $(LDFLAGS) $(LDLIBS) -o "$$bin" || exit 1; \
		else \
			echo "SKIP:  $$src (no main)"; \
		fi; \
	done

clean:
	rm -rf build
	rm -f src/*.o
	find tests -maxdepth 1 -type f -executable -delete

.PHONY: all tests clean

# ---------------------------------------------------------------------------
# Test suite
# ---------------------------------------------------------------------------

TEST_DIR := tests

TEST_SOURCES := \
	$(TEST_DIR)/test_directory_delete.c \
	$(TEST_DIR)/test_directory_delete_nonempty.c \
	$(TEST_DIR)/test_directory_files.c \
	$(TEST_DIR)/test_directory_lookup.c \
	$(TEST_DIR)/test_directory_nested.c \
	$(TEST_DIR)/test_file_create.c \
	$(TEST_DIR)/test_file_delete.c \
	$(TEST_DIR)/test_file_extent_limit.c \
	$(TEST_DIR)/test_file_io.c \
	$(TEST_DIR)/test_file_multiblock.c \
	$(TEST_DIR)/test_file_overwrite.c \
	$(TEST_DIR)/test_file_rename.c \
	$(TEST_DIR)/test_file_reuse.c \
	$(TEST_DIR)/test_filesystem.c \
	$(TEST_DIR)/test_inode_allocator.c \
	$(TEST_DIR)/test_nested_file_delete.c \
	$(TEST_DIR)/test_path_resolution.c \
	$(TEST_DIR)/test_rename_safety.c

TEST_BINS := $(TEST_SOURCES:.c=)

TEST_CFLAGS := $(CFLAGS) \
	-Iinclude \
	-I../../include \
	-I../../shared/include

$(TEST_DIR)/%: $(TEST_DIR)/%.c build/libnbfs.a
	$(CC) $(TEST_CFLAGS) $< build/libnbfs.a -o $@


.PHONY: test test-build test-clean

test-build: $(TEST_BINS)

test:
	@$(MAKE) test-build
	@echo
	@echo "========================================"
	@echo " NeoBench libnbfs test suite"
	@echo "========================================"
	@failed=0; \
	for test in $(TEST_BINS); do \
		printf "%-42s" "$$(basename "$$test")"; \
		if "$$test" >/tmp/nbfs-test-output 2>&1; then \
			echo "PASS"; \
		else \
			echo "FAIL"; \
			cat /tmp/nbfs-test-output; \
			failed=1; \
		fi; \
	done; \
	echo; \
	echo "========================================"; \
	if [ "$$failed" -eq 0 ]; then \
		echo " ALL TESTS PASSED"; \
	else \
		echo " TEST FAILURES DETECTED"; \
		exit 1; \
	fi; \
	echo "========================================"

test-clean:
	rm -f $(TEST_BINS)
