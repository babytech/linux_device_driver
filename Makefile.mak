ifeq ($(ENV_CC_COMPILE),)
$(error "run command: 'source source_me_before_compiling' in top dir")
endif
ifeq ($(ENV_KERNEL_DIR),)
$(error "run command: 'source source_me_before_compiling' in top dir")
endif

A            := ar
ARCH         := arm
CC_COMPILE   := $(ENV_CC_COMPILE)
KDIR         := $(ENV_KERNEL_DIR)
HDIR         := $(PWD)/../generic
EXTRA_CFLAGS += -I$(INCDIR)
MAKE         := make

all:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) INCDIR=$(HDIR) M=$(PWD) modules CROSS_COMPILE=$(CC_COMPILE)

.PHONY : clean
clean:
	cd $(PWD) && rm -rf *.o *~ *.ko *mod* .cache* .*.cmd *.symvers .tmp_versions
