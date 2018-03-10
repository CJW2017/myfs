EXTRA_CFLAGS += -O0
EXTRA_CFLAGS += -gdwarf-2

ifneq ($(KERNELRELEASE),)
	obj-m += myfs.o
	myfs-objs:=  inode.o dir.o namei.o file.o super.o
else
	PWD:=$(shell pwd)
#   KERNELDIR?=/usr/src/linux-source-4.4.0/linux-source-4.4.0
	KERNELDIR?=/lib/modules/$(shell uname -r)/build
#    KERNELDIR?=/home/jks/Desktop/linux-4.6_mb
all:
	make -C $(KERNELDIR) M=$(PWD)  modules
clean:
	make -C $(KERNELDIR) M=$(PWD)  clean
endif
