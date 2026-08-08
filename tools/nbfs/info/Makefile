CC=gcc

CFLAGS=-Wall -Wextra \
-I../../../include \
-I../../../libs/libnbfs/include

LDFLAGS=../../../libs/libnbfs/build/libnbfs.a

TARGET=nbfs-info

all:
	$(CC) $(CFLAGS) src/nbfs-info.c $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)
