ifneq ($(ENV_CC_COMPILE),)
CC_COMPILE = $(ENV_CC_COMPILE)
else
CC_COMPILE = arm-linux-gnueabi-
endif

CC_COMPILE_GCC = $(CC_COMPILE)gcc

apps = file_reader_app gfifo_poll_app gfifo_poll_n_app gfifo_signal_app  \
       calamares_app clearmem_app dump_memory_to_file_app smemcap_app    \
       limitfs_app mem_util_app asciidump_app

all: $(apps)

file_reader_app:
	$(CC_COMPILE_GCC) -o $@ file_reader.c

gfifo_poll_app:
	$(CC_COMPILE_GCC) -o $@ gfifo_poll.c

gfifo_poll_n_app:
	$(CC_COMPILE_GCC) -o $@ gfifo_poll_n.c

gfifo_signal_app:
	$(CC_COMPILE_GCC) -o $@ gfifo_signal.c

calamares_app:
	$(CC_COMPILE_GCC) -o $@ calamares_bin.c

clearmem_app:
	$(CC_COMPILE_GCC) -o $@ clearmem.c

dump_memory_to_file_app:
	$(CC_COMPILE_GCC) -o $@ dump_memory_to_file.c

smemcap_app:
	$(CC_COMPILE_GCC) -o $@ smemcap.c

limitfs_app:
	$(CC_COMPILE_GCC) -o $@ limitfs.c -D_FILE_OFFSET_BITS=64 -lfuse

mem_util_app:
	$(CC_COMPILE_GCC) -o $@ memutil/mem_util.c memutil/mem_ops.c memutil/work_paras.c

asciidump_app:
	$(CC_COMPILE_GCC) -o $@ asciidump.c

list:
	@echo $(apps)

clean:
	@rm -rf *_app
