DEBFLAGS = -O2
#DEBFLAGS = -O -g -DDEBUG

EXTRA_CFLAGS += $(DEBFLAGS)

obj-m += tlx00.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
