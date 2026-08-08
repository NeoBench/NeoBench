CC ?= gcc

CFLAGS = -Wall -Wextra -O2 -Iinclude

SRC = $(wildcard src/*.c)

OBJ = $(SRC:src/%.c=build/obj/%.o)

TARGET = build/bin/mkfs.nbfs

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p build/bin
	$(CC) $(OBJ) -o $(TARGET)

build/obj/%.o: src/%.c
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build

run: all
	./$(TARGET)

.PHONY: all clean run
