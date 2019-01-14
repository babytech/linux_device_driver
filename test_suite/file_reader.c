#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define BUFF_SIZE 1024

int main(int argc, char *argv[])
{
	int fd;
	int ret;
	char buff[BUFF_SIZE];

	if (argc < 2) {
		printf("Usage: %s <filename> [nonblock]\n", argv[0]);
		return -1;
	}

	if ((argc > 2) && !strncmp("nonblock", argv[2], strlen("nonblock"))) {
		fd = open(argv[1], O_RDONLY | O_NONBLOCK);
	} else {
		fd = open(argv[1], O_RDONLY);
	}

	if (fd < 0) {
		printf("Error on opening file %s\n", argv[1]);
		return -2;
	}

	for (;;) {
		ret = read(fd, buff, BUFF_SIZE - 1);

		if (ret > 0) {
			buff[ret] = 0;
			printf("read length: %d, data: %s\n", ret, buff);
		} else if (ret == 0) {
			printf("no data\n");
			break;
		} else {
			if (errno == EINTR) {
				printf("ERROR: EINTR\n");
			} else if (errno == EAGAIN) {
				printf("ERROR: EAGAIN\n");
			} else {
				printf("ERROR: %d\n", errno);
				break;
			}
		}
	}

	close(fd);
	return ret;
}
