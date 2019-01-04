### the ko name should be the same as the C file
### this is for arm-linux-gcc on ARM
A	:=ar
ARCH	:=arm
CC_COMPILE= arm-linux-gnueabi-
obj-m   :=gfifo_platform_driver.o gfifo_platform_device.o
KDIR    :=/home/babytech/EDU/buildroot-2017.02.4/output/build/linux-4.9.6
PWD     :=$(shell pwd)
MAKE    :=make
all:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(PWD) modules CROSS_COMPILE=$(CC_COMPILE)
clean:
	rm -rf *.o *~ *.ko *mod* *.symvers .gfifo_platform_*.* .tmp_versions
