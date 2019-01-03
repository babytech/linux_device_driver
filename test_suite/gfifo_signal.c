#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_LEN 100

static int fd;

static void input_handler(int signal_num)
{
	char buf[MAX_LEN] = {0};
	int len;

	printf("\n%s: signal-%u received\n", __func__, signal_num);
	len = read(fd, &buf, MAX_LEN - 1);
	buf[len] = '\0';
	printf("receive data: %s\n", buf);
}

int main(int argc, char *argv[])
{
	int o_flags;

	if (argc < 2) {
		printf("Help : %s [device]\n", argv[0]);
		printf("usage : %s /dev/gfifo\n", argv[0]);
		return -1;
	}

	fd = open(argv[1], O_RDWR, S_IRUSR|S_IWUSR);
	if (fd == -1) {
		printf("%s: device %s - open failed\n", __func__, argv[1]);
		return -2;
	}

	signal(SIGIO, input_handler);
	fcntl(fd, F_SETOWN, getpid());
	o_flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, o_flags|FASYNC);

	while (1) {
		printf("%s: waiting for read the data from %s\n", __func__, argv[1]);
		sleep(30);
	}

	return 0;
}
