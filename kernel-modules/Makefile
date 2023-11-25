ifneq ($(KERNELRELEASE),)
obj-m  := dm-simple.o dm-dust.o dm-hello.o

else
KDIR ?= /usr/src/linux-headers-$(shell uname -r)

default:
	$(MAKE) -C $(KDIR) M=$$PWD
clean:
	rm -f *.o .ko *.mod*
endif
