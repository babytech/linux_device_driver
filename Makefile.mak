### the ko name should be the same as the C file
### this is for arm-linux-gcc on ARM
A          :=ar
ARCH       := arm
CC_COMPILE := $(ENV_CC_COMPILE)
KDIR       := $(ENV_KERNEL_DIR)
PWD        := $(ENV_DRIVER_DIR)
HDIR    :=$(PWD)/../generic
EXTRA_CFLAGS += -I$(INCDIR)
MAKE       := make

all:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) INCDIR=$(HDIR) M=$(PWD) modules CROSS_COMPILE=$(CC_COMPILE)

.PHONY : clean
clean:
	cd $(PWD) && rm -rf *.o *~ *.ko *mod* .*.cmd *.symvers .tmp_versions
