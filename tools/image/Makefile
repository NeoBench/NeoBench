CC = gcc

CFLAGS = \
    -O2 \
    -Wall \
    -Wextra \
    -I../../libs/libnbfs/include

LDFLAGS =

LIBNBFS = ../../libs/libnbfs/build/libnbfs.a

TARGET = nbimage

SRCS = \
main.c \
image.c

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS) $(LIBNBFS)
	$(CC) -o $@ $(OBJS) $(LIBNBFS)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
