CC ?= m68k-elf-gcc

SRC := $(wildcard *.c)
OBJ := $(SRC:.c=.o)

CFLAGS := \
-Wall \
-Wextra \
-std=c11 \
-O2 \
-Iinclude

all: $(OBJ)

clean:
	rm -f *.o

.PHONY: all clean
