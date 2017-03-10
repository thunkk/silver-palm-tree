obj-m := testingfs.o
testingfs-objs := testfs.o

CFLAGS_testfs.o := -DDEBUG

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
