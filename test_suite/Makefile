ifneq ($(ENV_CC_COMPILE),)
CC_COMPILE = $(ENV_CC_COMPILE)
else
CC_COMPILE = arm-linux-gnueabi-
endif

CC_COMPILE_GCC = $(CC_COMPILE)gcc

apps = file_reader_app gfifo_poll_app gfifo_poll_n_app gfifo_signal_app

all: $(apps)

file_reader_app:
	$(CC_COMPILE_GCC) file_reader.c  -o file_reader_app

gfifo_poll_app:
	$(CC_COMPILE_GCC) gfifo_poll.c   -o gfifo_poll_app

gfifo_poll_n_app:
	$(CC_COMPILE_GCC) gfifo_poll_n.c -o gfifo_poll_n_app

gfifo_signal_app:
	$(CC_COMPILE_GCC) gfifo_signal.c -o gfifo_signal_app

list:
	@echo $(apps)

clean:
	@rm -rf *_app
