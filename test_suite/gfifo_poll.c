#include <stdio.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/epoll.h>
/* According to POSIX.1-2001 */
#include <sys/select.h>
/* According to earlier standards */
#include <sys/time.h>
#include <unistd.h>

#define FIFO_CLEAR 0x1
#define BUFFER_LEN 20

static int poll_body(int fd)
{
	int ret = 0;
	fd_set r_set, w_set;

	ret = ioctl(fd, FIFO_CLEAR, 0);
	if (ret)
		printf("%s: ioctl clear fifo failed!\n", __func__);

	while (1) {
		FD_ZERO(&r_set);
		FD_ZERO(&w_set);
		FD_SET(fd, &r_set);
		FD_SET(fd, &w_set);

		ret = select(fd + 1, &r_set, &w_set, NULL, NULL);

		if (FD_ISSET(fd, &r_set))
			printf("%s: fifo could be read...\n", __func__);

		if (FD_ISSET(fd, &w_set))
			printf("%s: fifo could be writen...\n", __func__);

		/* sleeping 15 seconds */
		sleep(15);
	}

	return ret;
}

static int epoll_body(int fd)
{
	int ret = -1;
	int epfd;
	unsigned int timeout;
	struct epoll_event ev_gfifo;

	ret = ioctl(fd, FIFO_CLEAR, 0);
	if (ret)
		printf("%s: ioctl clear fifo failed!\n", __func__);

	epfd = epoll_create(1);
	if (epfd < 0) {
		perror("epoll_create()");
		goto fail;
	}

	bzero(&ev_gfifo, sizeof(struct epoll_event));

	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev_gfifo);
	if (ret < 0) {
		perror("epoll_ctl()");
		goto fail;
	}

	/* timeout is 15 seconds */
	timeout = 15000;
	ret = epoll_wait(epfd, &ev_gfifo, 1, timeout);
	if (ret < 0) {
		perror("epoll_wait()");
	} else if (ret == 0) {
		printf("%s: NO data input in FIFO within %u seconds.\n", __func__, timeout);
	} else {
		printf("%s: FIFO is not empty\n", __func__);
	}

	ret = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev_gfifo);
	if (ret < 0) {
		perror("epoll_ctl()");
		goto fail;
	}
fail:
	return ret;
}

static void signal_callback(int signal)
{
	printf("%s: signal-%d is captured\n", __func__, signal);
}

static int signal_init()
{
	printf("%s: has been called\n", __func__);
	int ret = 0;
	struct sigaction act, oldact;
	act.sa_handler = signal_callback;
	act.sa_flags = 0;

	sigemptyset(&act.sa_mask);
	sigaddset(&act.sa_mask, SIGQUIT);
	sigaddset(&act.sa_mask, SIGTSTP);

	ret = sigaction(SIGINT, &act, &oldact);
	if (ret < 0) {
		perror("sigaction error");
	}
	return ret;
}

int main(int argc, char *argv[])
{
	int fd;
	char ch;
	char *dev;

	if (signal_init() < 0)
		_exit(1);

	if (argc < 3) {
		printf("Help: %s [-p]/[-e] [device]\n", argv[0]);
		printf("usage: %s -p /dev/gfifo\n", argv[0]);
		return -1;
	}

	ch = argv[1][1];
	dev = argv[2];
	fd = open (argv[2], O_RDONLY|O_NONBLOCK);
	if (fd != -1) {
		switch (ch) {
		case 'P':
		case 'p':
			printf("poll_body will be used\n");
			poll_body(fd);
			break;
		case 'E':
		case 'e':
			printf("epoll_body will be used\n");
			epoll_body(fd);
			break;
		default:
			break;
		}
		close(fd);
	} else {
		printf("Device open failed!\n");
	}

	return 0;
}
