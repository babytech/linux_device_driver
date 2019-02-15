#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/socket.h>

#define NETLINK_TEST     25
#define MAX_PAYLOAD      1024

int main(int argc, char *argv[])
{
	int sock_fd;
	struct sockaddr_nl src_addr, dest_addr;
	struct nlmsghdr *nlh = NULL;
	struct iovec iov;
	struct msghdr msg;
	int state;
	int state_smg = 0;

	/* create a socket */
	sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_TEST);
	if (sock_fd == -1) {
		printf("error getting socket: %s", strerror(errno));
		return -1;
	}

	/* to prepare binding */
	memset(&msg, 0, sizeof(msg));
	memset(&src_addr, 0, sizeof(src_addr));

	/* src address */
	src_addr.nl_family = AF_NETLINK;
	src_addr.nl_pid = getpid();
	src_addr.nl_groups = 0;
	bind(sock_fd, (struct sockaddr *) &src_addr, sizeof(src_addr));

	/* destination address */
	memset(&dest_addr, 0, sizeof(dest_addr));
	dest_addr.nl_family = AF_NETLINK;
	dest_addr.nl_pid = 0;
	dest_addr.nl_groups = 0;

	/* Fill the netlink message header */
	nlh = (struct nlmsghdr *) malloc(NLMSG_SPACE(MAX_PAYLOAD));
	nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
	nlh->nlmsg_pid = getpid();
	nlh->nlmsg_flags = 0;

	/* Fill in the netlink message payload */
	strcpy(NLMSG_DATA(nlh), "connect to kernel");
	iov.iov_base = (void *) nlh;
	iov.iov_len = nlh->nlmsg_len;

	/* iov.iov_len = NLMSG_SPACE(MAX_PAYLOAD); */
	memset(&msg, 0, sizeof(msg));
	msg.msg_name = (void *) &dest_addr;
	msg.msg_namelen = sizeof(dest_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	printf("sending message...\n");
	state = sendmsg(sock_fd, &msg, 0);
	if (state == -1) {
		printf("get error sendmsg=%s\n", strerror(errno));
	}

	memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));

	while (1) {
		printf("waiting kernel message. ...\n");
		state = recvmsg(sock_fd, &msg, 0);
		if (state < 0) {
			printf("get error recvmsg=%s\n", strerror(errno));
		}
		printf("received kernel message: %s\n", (char *) NLMSG_DATA(nlh));
	}

	close(sock_fd);
	return 0;
}
