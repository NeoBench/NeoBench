CC ?= m68k-elf-gcc

CFLAGS := -Wall -Wextra -std=c11 -O2 -Iinclude

SRC := $(shell find . -name '*.c')
OBJ := $(SRC:.c=.o)

all: $(OBJ)

clean:
	find . -name '*.o' -delete

.PHONY: all clean
