CC ?= gcc

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

CFLAGS := -Wall -Wextra -std=c11 -O2 -Iinclude

all: nbbuild

nbbuild: $(OBJ)
	$(CC) $(OBJ) -o nbbuild

clean:
	rm -f src/*.o nbbuild

.PHONY: all clean
