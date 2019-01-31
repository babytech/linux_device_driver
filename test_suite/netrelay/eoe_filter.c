/*
   This software runs well if the same kind of configuration is done on your linux box

   tunctl -t tap-fwd
   ifconfig tap-fwd 169.254.17.4 up
   /sbin/sysctl -w net.ipv4.conf.all.forwarding=1 2>&1 | grep -v forwarding
   iptables -t nat -A POSTROUTING -j MASQUERADE -o tap-fwd
   iptables -t nat -A PREROUTING -i tap-fwd -p tcp --dport 23 -j DNAT --to 10.0.0.1:23

 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <unistd.h>

#include <asm/types.h>
#include <arpa/inet.h>

#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h> 
#include <linux/if_arp.h> 
#include <linux/filter.h>

#include <sys/ioctl.h> 
#include <sys/types.h>          
#include <sys/socket.h>
#include <sys/stat.h>

#include <net/ethernet.h>
#include <netinet/ip.h>

#include "relay.h"

#define EOE_ADD_HEADER        SERVICE_TO_NET
#define EOE_DEL_HEADER        NET_TO_SERVICE
#define EOE_HEADER_SIZE       (sizeof(eoeHeader))
#define EOE_MIN_PAYLOAD_SIZE  64

#pragma pack(1)
typedef struct eoeHeader_s
{
    unsigned char dmac[6];
    unsigned char smac[6];
    unsigned int tag;
    unsigned short len;
    unsigned char op_code:4;
    unsigned char downstream:1;
    unsigned char flag:3;
    union
    {
        unsigned short vlanport;
        unsigned short pon:4;
        unsigned short gem_port:12;
    } itf;
    unsigned char protocol_id;
} eoeHeader;
#pragma pack()

static struct sock_filter packetFilter[] = {
    { 0x30, 0, 0, 0x00000012}, /* EOE protocol code: 1*/
    { 0x54, 0, 0, 0x000000f0},
    { 0x15, 0, 3, 0x00000010},

    { 0x28, 0, 0, 0x00000026}, /* Protocol Type: Not 0x08ab */
    { 0x15, 1, 0, 0x000008ab},

    { 0x06, 0, 0, 0x0000ffff},
    { 0x06, 0, 0, 0x00000000},
};
const static unsigned short packetFilterSize = sizeof(packetFilter)/sizeof(packetFilter[0]);

static int packetBufferInit(buffer **pbuf, unsigned priv);
static void packetBufferPrepare(buffer *pbuf, unsigned priv);
static void packetDump(buffer *pbuf, char *desc);
static void packetEdit(buffer *pbuf, unsigned priv);
static void addEoeHeader(buffer *pbuf);
static void delEoeHeader(buffer *pbuf);
static void initEoeHeader(eoeHeader *hdr, size_t payloadSize);
static void extendPacketPayload(buffer *pbuf);

#define EOE_FILTER_LOG(priority,format,...) myLogFun(__FILE__, __LINE__,__func__, priority, format, ##__VA_ARGS__)

static int openTapItf(struct relay_info *info)
{
    char *dev = info->itf;
    struct ifreq ifr = {};
    int fd, err;
    const char *clonedev = "/dev/net/tun";

    /* open the clone device */
    EOE_FILTER_LOG(LOG_INFO, "Opening clone device %s\n", clonedev);
    if ((fd = open(clonedev, O_RDWR)) == -1) {
        EOE_FILTER_LOG(LOG_ERR, "Couldn't open %s device. Errno: %d %s\n", clonedev, errno, strerror(errno));
        return fd;
    }

    EOE_FILTER_LOG(LOG_INFO, "Socket of tap interface %s is %d\n", dev, fd);
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

    EOE_FILTER_LOG(LOG_INFO, "Attaching socket %d to tap interface %s\n", fd, dev);
    if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) == -1) {
        EOE_FILTER_LOG(LOG_ERR, "Couldn't get tap interface %s. Errno: %d %s\n", dev, errno, strerror(errno));
        close(fd);
        return err;
    }
    EOE_FILTER_LOG(LOG_INFO, "Tap interface %s is successfully created\n", dev);

    return fd;
}

static int createRawSocketAndBindToInterface(struct relay_info *info)
{
    char* itfName = info->itf;
    int type = info->type;
    int raw_socket_fd;
    EOE_FILTER_LOG(LOG_INFO, "creating the socket for %s\n", itfName);
    if ((raw_socket_fd = socket(AF_PACKET, type, htons(ETH_P_ALL))) == -1) {
        EOE_FILTER_LOG(LOG_ERR, "Impossible to create the socket for %s: %s\n", itfName, strerror(errno));
        return -1;
    }
    EOE_FILTER_LOG(LOG_INFO, "The socket for %s is %d\n", itfName, raw_socket_fd);

    if (!setItfToPromiscModeAndDisableArpProtocol(raw_socket_fd, itfName)) {
        EOE_FILTER_LOG(LOG_ERR, "%s couldn't be set to promiscuous mode\n", itfName);
        return -1;
    }

    struct sockaddr_ll sckaddr = {};
    if (!getLLsocketOf(raw_socket_fd, itfName, &sckaddr)) {
        EOE_FILTER_LOG(LOG_ERR, "Couldn't get the low-level socket of %s\n", itfName);
        return -1;
    }
    EOE_FILTER_LOG(LOG_INFO, "Binding socket %d to %s\n", raw_socket_fd, itfName);
    if (bind(raw_socket_fd, (struct sockaddr *) &sckaddr, sizeof (sckaddr)) < 0) {
        EOE_FILTER_LOG(LOG_ERR, "Couldn't attach to interface %s: %s\n", itfName, strerror(errno));
        return -1;
    }
    return raw_socket_fd;
}

static int packetBufferInit(buffer **pbuf, unsigned priv)
{
    size_t size = IP_MAXPACKET;
    if ((priv == EOE_ADD_HEADER) || (priv == EOE_DEL_HEADER))
        size += EOE_HEADER_SIZE;
    return bufferInit(pbuf, size);
}

static void packetBufferPrepare(buffer *pbuf, unsigned priv)
{
    bufferReset(pbuf);
    if (priv == EOE_ADD_HEADER)
        pbuf->data += EOE_HEADER_SIZE;
}

static void packetDump(buffer *pbuf, char *desc)
{
    unsigned int i;

    if (get_logLevel() <= 1)
        return;

    EOE_FILTER_LOG(LOG_DEBUG, "%s", desc);
    for (i = 0; i < pbuf->size; i++) {
        if (i%16 == 0)
            EOE_FILTER_LOG(LOG_DEBUG, "\n");
        EOE_FILTER_LOG(LOG_DEBUG, "%02X ", (unsigned char) pbuf->data[i]);
    }
    EOE_FILTER_LOG(LOG_DEBUG, "\nTotal size: %d\n", pbuf->size);
}

static void packetEdit(buffer *pbuf, unsigned priv)
{
    packetDump(pbuf, "## Before packet edit(original data)");
    switch (priv) {
        case EOE_ADD_HEADER: addEoeHeader(pbuf); break;
        case EOE_DEL_HEADER: delEoeHeader(pbuf); break;
        default: break;
    }
    packetDump(pbuf, "## After packet edit(data will be forward)");
}

static void addEoeHeader(buffer *pbuf)
{
    eoeHeader header;

    extendPacketPayload(pbuf);
    initEoeHeader(&header, pbuf->size);

    pbuf->data -= EOE_HEADER_SIZE;
    pbuf->size += EOE_HEADER_SIZE;
    memcpy(pbuf->data, &header, EOE_HEADER_SIZE);
}

static void initEoeHeader(eoeHeader *hdr, size_t payloadSize)
{
#define OP_A2A_Switch     8
#define DIR_UPSTREAM      0

    const unsigned char  obcCore0MacAddr[]  = { 0x06, 0x00, 0x00, 0x00, 0x00, 0x78 };
    const unsigned char  sharpNoseMacAddr[] = { 0x06, 0x00, 0x00, 0x00, 0x00, 0xF8 };

    memset(hdr, 0, sizeof(eoeHeader));
    memcpy(hdr->dmac, sharpNoseMacAddr,6);
    memcpy(hdr->smac, obcCore0MacAddr, 6);
    hdr->tag = 0x81000FFE;
    /* Total size: EOE header size(skip DMAC,SMAC,TAG,LEN) + Payload size */
    hdr->len = payloadSize + (EOE_HEADER_SIZE - 6 - 6 - 4 - 2);
    hdr->op_code = OP_A2A_Switch;
    hdr->downstream = DIR_UPSTREAM;
}

static void extendPacketPayload(buffer *pbuf)
{
    if (pbuf->size < EOE_MIN_PAYLOAD_SIZE) {
        memset(pbuf->data + pbuf->size,
                0,
                EOE_MIN_PAYLOAD_SIZE - pbuf->size);
        pbuf->size = EOE_MIN_PAYLOAD_SIZE;
    }
}

static void delEoeHeader(buffer *pbuf)
{
    pbuf->data += EOE_HEADER_SIZE;
    pbuf->size -= EOE_HEADER_SIZE;
}

static void attachItfFilterForService(int itf)
{
    attachItfFilter(itf, packetFilter, packetFilterSize);
}

static struct relay_info eoe_info = {
    .id = RELAY_ID_EOE,
    .name = "EOE",
    .ops = {
        .parse_cmdline = NULL,
        .attach_filter = attachItfFilterForService,
        .create_network_interface = createRawSocketAndBindToInterface,
        .create_service_interface = openTapItf,
    },
    .pkg = {
        .bufferInit = packetBufferInit,
        .bufferPrepare = packetBufferPrepare,
        .dump = packetDump,
        .edit = packetEdit,
    },
};

int service_init()
{
    int ret = relay_register(&eoe_info);
    if (ret)
        EOE_FILTER_LOG(LOG_INFO, "register EOE relay fail\n");
    return ret;
}
