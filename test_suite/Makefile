ifneq ($(ENV_CC_COMPILE),)
CC_COMPILE = $(ENV_CC_COMPILE)
else
CC_COMPILE = arm-linux-gnueabi-
endif

CC_COMPILE_GCC = $(CC_COMPILE)gcc

apps = file_reader_app gfifo_poll_app gfifo_poll_n_app gfifo_signal_app \
	calamares_app clearmem_app dump_memory_to_file_app smemcap_app \
	limitfs_app mem_util_app

all: $(apps)

file_reader_app:
	$(CC_COMPILE_GCC) file_reader.c  -o file_reader_app

gfifo_poll_app:
	$(CC_COMPILE_GCC) gfifo_poll.c   -o gfifo_poll_app

gfifo_poll_n_app:
	$(CC_COMPILE_GCC) gfifo_poll_n.c -o gfifo_poll_n_app

gfifo_signal_app:
	$(CC_COMPILE_GCC) gfifo_signal.c -o gfifo_signal_app

calamares_app:
	$(CC_COMPILE_GCC) calamares_bin.c -o calamares_app

clearmem_app:
	$(CC_COMPILE_GCC) clearmem.c -o clearmem_app

dump_memory_to_file_app:
	$(CC_COMPILE_GCC) dump_memory_to_file.c -o dump_memory_to_file_app

smemcap_app:
	$(CC_COMPILE_GCC) smemcap.c -o smemcap_app

limitfs_app:
	$(CC_COMPILE_GCC) limitfs.c -D_FILE_OFFSET_BITS=64 -lfuse -o limitfs_app

mem_util_app:
	$(CC_COMPILE_GCC) memutil/mem_util.c memutil/mem_ops.c memutil/work_paras.c -o mem_util_app

list:
	@echo $(apps)

clean:
	@rm -rf *_app
