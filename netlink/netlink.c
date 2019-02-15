#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/types.h>
#include <net/sock.h>
#include <net/netlink.h>

#define NETLINK_TEST     25
#define MAX_MSGSIZE      1024

struct sock *nl_sk = NULL;

void send_netlink_data(int pid, char *message, int len)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;

	skb = alloc_skb(NLMSG_SPACE(MAX_MSGSIZE), GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR "NETLINK: alloc_skb error\n");
		return;
	}

	nlh = nlmsg_put(skb, 0, 0, 0, MAX_MSGSIZE, 0);

	NETLINK_CB(skb).portid = 0;
	NETLINK_CB(skb).dst_group = 0;

	memcpy(NLMSG_DATA(nlh), message, len + 1);

	printk("send_netlink_data: pid=%d, message=%s\n", pid, (char *) NLMSG_DATA(nlh));

	netlink_unicast(nl_sk, skb, pid, MSG_DONTWAIT);
}

void recv_netlink_data(struct sk_buff *__skb)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	char *k_msg = "from kernel messages!";
	int k_msg_len = strlen(k_msg);

	printk("recv_netlink_data: data is ready to read\n");

	skb = skb_get(__skb);
	if (skb->len < NLMSG_SPACE(0)) {
		kfree_skb(skb);
		return;
	}

	nlh = nlmsg_hdr(skb);

	printk("recv_netlink_data: message received: %s\n", (char *) NLMSG_DATA(nlh));

	send_netlink_data(nlh->nlmsg_pid, k_msg, k_msg_len);

	kfree_skb(skb);
}

struct netlink_kernel_cfg cfg = {
	.input = recv_netlink_data,
};

int netlink_init(void)
{
	printk("NETLINK: module init\n");
	nl_sk = netlink_kernel_create(&init_net, NETLINK_TEST, &cfg);
	if (!nl_sk) {
		printk(KERN_ERR "NETLINK: create netlink socket error\n");
		return 1;
	}
	printk("NETLINK: create netlink socket ok\n");
	return 0;
}

static void netlink_exit(void)
{
	if (nl_sk) {
		sock_release(nl_sk->sk_socket);
	}

	printk("NETLINK: module exit\n");
}

module_init(netlink_init);
module_exit(netlink_exit);
