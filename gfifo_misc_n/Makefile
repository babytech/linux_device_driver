### the ko name should be the same as the C file
### this is for arm-linux-gcc on ARM
A	:=ar
ARCH	:=arm
CC_COMPILE= arm-linux-gnueabi-
obj-m   :=gfifo_misc_n.o gfifo_platform_device_n.o
KDIR    :=/home/ubuntu/EDU/linux-4.12
PWD     :=$(shell pwd)
MAKE    :=make
all:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(PWD) modules CROSS_COMPILE=$(CC_COMPILE)
clean:
	rm -rf *.o *~ *.ko *mod* *.symvers .gfifo_misc_n*.* .gfifo_platform_*.* .tmp_versions
