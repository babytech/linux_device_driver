#ifndef __RELAY_H_
#define __RELAY_H_
enum service_id {
	SERVICE_TO_NET = 0x1,
	NET_TO_SERVICE = 0x2
};

enum relay_id {
	RELAY_ID_DHCP = 0x0,
	RELAY_ID_EOE = 0x1,
	_RELAY_ID_MAX = 0x2
};

typedef struct progArgs_s
{
	char *netItf;
	char *serviceItf;
} progArgs;

typedef struct threadArgs_s
{
	int from_fd;
	int to_fd;
	char *name;
	unsigned int priv;
} threadArgs;

typedef struct buffer_s
{
	char *base;
	size_t allocated;
	char *data;
	size_t size;
} buffer;

struct relay_info;

struct custom_handle_func {
	int (*parse_cmdline)(struct relay_info *info);
	void (*attach_filter)(int itf);
	int (*create_network_interface)(struct relay_info *info);
	int (*create_service_interface)(struct relay_info *info);
};

struct custom_packet_func {
	int (*bufferInit)(buffer **pbuf, unsigned priv);
	void (*bufferPrepare)(buffer *pbuf, unsigned priv);
	void (*dump)(buffer *pbuf, char *desc);
	void (*edit)(buffer *pbuf, unsigned priv);
};

struct relay_info {
	int id;
	const char *name;
	void *private;
	int type;
	char *itf;
	struct custom_handle_func ops;
	struct custom_packet_func pkg;
};

struct relay_info *relay_find(enum relay_id id);
int relay_register(struct relay_info *info);
int get_logLevel(void);

int bufferInit(buffer **pbuf, size_t size);
int bufferReset(buffer *pbuf);
size_t bufferDataToTail(buffer *pbuf);
void bufferSetDataSize(buffer *pbuf, size_t size);

void myLogFun(char* fileName, int lineNum, const char *fun, int priority, const char* format, ...);
int getLLsocketOf(int fd, char *name, struct sockaddr_ll *ll_sockaddr);
int getMacAddressOf(int raw_socket_fd, char *name, unsigned char *macAddrTab);
void attachItfFilter(int itf, struct sock_filter *filter_data, unsigned short filter_len);
int setItfToPromiscModeAndDisableArpProtocol(int fd, char *name);
#endif
