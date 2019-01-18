#include <stdio.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
/* According to POSIX.1-2001 */
#include <sys/select.h>
/* According to earlier standards */
#include <sys/time.h>
#include <unistd.h>

#define FIFO_CLEAR 0x1
#define BUFFER_LEN 20
#define FD_ARRAY_SIZE 20

static int poll_body(int *fd_array, char **dev_array, int num)
{
	int i, ret = 0;
	fd_set r_set, w_set;

	for (i = 0; i < num; i++) {
		ret = ioctl(fd_array[i], FIFO_CLEAR, 0);
		if (ret)
			printf("%s: ioctl clear fifo (%s) failed!\n", __func__, dev_array[i]);
	}

	while (1) {
		FD_ZERO(&r_set);
		FD_ZERO(&w_set);

		for (i = 0; i < num; i++) {
			FD_SET(fd_array[i], &r_set);
			FD_SET(fd_array[i], &w_set);
		}

		ret = select(fd_array[num-1]+1, &r_set, &w_set, NULL, NULL);

		for (i = 0; i < num; i++) {
			if (FD_ISSET(fd_array[i], &r_set))
				printf("%s: fifo (%s) could be read...\n", __func__, dev_array[i]);

			if (FD_ISSET(fd_array[i], &w_set))
				printf("%s: fifo (%s) could be writen...\n", __func__, dev_array[i]);
		}

		/* sleeping 15 seconds */
		sleep(15);
	}

	return ret;
}

static int epoll_body(int *fd_array, char **dev_array, int num)
{
	int ret = -1;
	int epfd;
	int i;
	unsigned int timeout;
	struct epoll_event ev_gfifo;
	struct epoll_event ev_gfifo_list[num];

	epfd = epoll_create(num);
	if (epfd < 0) {
		perror("epoll_create()");
		goto fail;
	}

	bzero(&ev_gfifo, sizeof(struct epoll_event));

	for (i = 0; i < num; i++) {
		ret = ioctl(fd_array[i], FIFO_CLEAR, i);
		if (ret)
			printf("%s: ioctl clear fifo (%s) failed!\n", __func__, dev_array[i]);

		ev_gfifo.events = EPOLLIN | EPOLLPRI;
		ev_gfifo.data.ptr = dev_array[i];

		ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd_array[i], &ev_gfifo);
		if (ret < 0) {
			perror("epoll_ctl()");
			goto fail;
		}
	}

	/* timeout is 15 seconds */
	timeout = 15000;
	ret = epoll_wait(epfd, ev_gfifo_list, num, timeout);
	if (ret < 0) {
		perror("epoll_wait()");
	} else if (ret == 0) {
		printf("%s: NO data input in FIFO within %u seconds.\n", __func__, timeout);
	} else {
		for (i = 0; i < ret; i++)
			printf("%s: FIFO (%s) is not empty\n", __func__, (char*)ev_gfifo_list[i].data.ptr);
	}

	for (i = 0; i < num; i++) {
		ret = epoll_ctl(epfd, EPOLL_CTL_DEL, fd_array[i], &ev_gfifo);
		if (ret < 0) {
			perror("epoll_ctl()");
			goto fail;
		}
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

static void close_fds(int *fd_array, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		if (fd_array[i] != -1)
			close(fd_array[i]);
	}
}

int main(int argc, char *argv[])
{
	int ret = 0;
	int i, fd_num, fd_array[FD_ARRAY_SIZE];
	char ch, *dev_array[FD_ARRAY_SIZE];

	if (signal_init() < 0)
		_exit(1);

	if (argc < 3) {
		printf("Help: %s [-p]/[-e] [device]\n", argv[0]);
		printf("usage: %s -p /dev/gfifo\n", argv[0]);
		return -1;
	}

	ch = argv[1][1];
	fd_num = argc -2;

	for (i = 0; i < fd_num; i++) {
		dev_array[i] = argv[i+2];
		fd_array[i] = open(argv[i+2], O_RDONLY|O_NONBLOCK);

		if (fd_array[i] == -1) {
			printf("Device %s open failed!\n", dev_array[i]);
			ret = -1;
		}
	}

	if (ret != -1) {
		switch (ch) {
		case 'P':
		case 'p':
			printf("poll_body will be used\n");
			poll_body(fd_array, dev_array, fd_num);
			break;
		case 'E':
		case 'e':
			printf("epoll_body will be used\n");
			epoll_body(fd_array, dev_array, fd_num);
			break;
		default:
			break;
		}
		close_fds(fd_array, fd_num);
	} else {
		printf("Device open failed!\n");
		close_fds(fd_array, fd_num);
	}

	return 0;
}
