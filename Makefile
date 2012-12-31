TARGET=arexxfs
LIBS=-lpthread -lfuse -lusb-1.0
CFLAGS=-Wall -O2 -I. -I/usr/include -I/usr/include/libusb-1.0 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=29

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

.PHONY: default all clean

default: $(TARGET)

all: default

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBS) -o $@

%.o: %.c $(HEADERS)
	$(CC) -c $(CFLAGS) $<

clean:
	rm -f *.o $(TARGET) $(OBJECTS)
