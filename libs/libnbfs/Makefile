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

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p build
	$(AR) rcs $(TARGET) $(OBJ)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build
	rm -f src/*.o

.PHONY: all clean
