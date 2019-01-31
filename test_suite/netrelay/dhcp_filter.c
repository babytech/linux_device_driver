/*
   Copyright (c) 2016 Nokia. All rights reserved.
   This program contains proprietary information which is a trade secret
   of Nokia and also is protected as an unpublished work under applicable
   Copyright laws. Recipient is to retain this program in confidence and
   is not permitted to use or make copies thereof other than as permitted
   in a written agreement with Nokia.
 */

/*
   ZTP adaption for mini-olt
   ZTP FEATURE: ALU02074765

   dhcp relay flow:   
   inband itf<---dhcp filter---oamVlan|dhcpVlan--->dummy-dhcp
   dhcpVlan = oamVlan-1

   packaget from inband itf must be with OAM vlan, untagged or other vlan is not supported

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

#define VLAN_DHCP2OAM   SERVICE_TO_NET
#define VLAN_OAM2DHCP   NET_TO_SERVICE

/* tcpdump -i tap-iwf udp dst port 68 -dd 
   { 0x28, 0, 0, 0x0000000c },
   { 0x15, 0, 4, 0x000086dd },
   { 0x30, 0, 0, 0x00000014 },
   { 0x15, 0, 11, 0x00000011 },
   { 0x28, 0, 0, 0x00000038 },
   { 0x15, 8, 9, 0x00000044 },
   { 0x15, 0, 8, 0x00000800 },
   { 0x30, 0, 0, 0x00000017 },
   { 0x15, 0, 6, 0x00000011 },
   { 0x28, 0, 0, 0x00000014 },
   { 0x45, 4, 0, 0x00001fff },
   { 0xb1, 0, 0, 0x0000000e },
   { 0x48, 0, 0, 0x00000010 },
   { 0x15, 0, 1, 0x00000044 },
   { 0x6, 0, 0, 0x00040000 },
   { 0x6, 0, 0, 0x00000000 },

   tcpdump -i tap-iwf udp dst port 68 -d
   (000) ldh      [12]
   (001) jeq      #0x86dd          jt 2    jf 6
   (002) ldb      [20]
   (003) jeq      #0x11            jt 4    jf 15
   (004) ldh      [56]
   (005) jeq      #0x44            jt 14   jf 15
   (006) jeq      #0x800           jt 7    jf 15
   (007) ldb      [23]
   (008) jeq      #0x11            jt 9    jf 15
   (009) ldh      [20]
   (010) jset     #0x1fff          jt 15   jf 11
   (011) ldxb     4*([14]&0xf)
   (012) ldh      [x + 16]
   (013) jeq      #0x44            jt 14   jf 15
   (014) ret      #262144
   (015) ret      #0
 */

static struct sock_filter packetFilter[] = {
    { 0x28, 0, 0, 0x0000000c },  /*tag*/
    { 0x15, 0, 13,0x00008100 },
    { 0x28, 0, 0, 0x0000000e },  /*vlan id*/
    { 0x54, 0, 0, 0x00000fff },
    { 0x15, 10,0, 0x00000ffe },  /*not 4094*/
    { 0x28, 0, 0, 0x00000010 },  /*eth type*/
    { 0x15, 0, 8, 0x00000800 },
    { 0x30, 0, 0, 0x0000001b },  /*udp*/
    { 0x15, 0, 6, 0x00000011 },
    { 0x28, 0, 0, 0x00000018 },  /*flags*/
    { 0x45, 4, 0, 0x00001fff },
    { 0xb1, 0, 0, 0x00000012 },  /*4*([18]&0xf), ip header size*/
    { 0x48, 0, 0, 0x00000014 },  /*dst port: mac+vlan+eth+dstport offset=12+4+2+2*/
    { 0x15, 0, 1, 0x00000044 },  /*dhcp client port 68*/
    { 0x6,  0, 0, 0x00040000 },
    { 0x6,  0, 0, 0x00000000 },
};


const static unsigned short packetFilterSize = sizeof(packetFilter)/sizeof(packetFilter[0]); 

#define DHCP_FILTER_LOG(priority,format,...) myLogFun(__FILE__, __LINE__,__func__, priority, format, ##__VA_ARGS__)

static int packetBufferInit(buffer **pbuf, unsigned priv);
static void packetBufferPrepare(buffer *pbuf, unsigned priv);
static void packetDump(buffer *pbuf, char *desc);
static void packetEdit(buffer *pbuf, unsigned priv);
static void setOamVlan(buffer *pbuf);
static void setDhcpVlan(buffer *pbuf);

static int packetBufferInit(buffer **pbuf, unsigned priv)
{
    size_t size = IP_MAXPACKET;
    return bufferInit(pbuf, size);
}

static void packetBufferPrepare(buffer *pbuf, unsigned priv)
{
    bufferReset(pbuf);
}

static void packetDump(buffer *pbuf, char *desc)
{
    unsigned int i;
    char line[64]={'\0'};

    if (get_logLevel() <= 1)
        return;

    DHCP_FILTER_LOG(LOG_DEBUG, "%s", desc);
    for (i = 0; i < pbuf->size; i++) {
        if (i%16 == 0) {
            DHCP_FILTER_LOG(LOG_DEBUG, "\n");
            DHCP_FILTER_LOG(LOG_DEBUG, "%s", line);
        }
        snprintf(&line[(i%16)*3],4,"%02X ",(unsigned char) pbuf->data[i]);
    }
    if(i%16)
        DHCP_FILTER_LOG(LOG_DEBUG, "\n%s\n", line);

    DHCP_FILTER_LOG(LOG_DEBUG, "\nTotal size: %d\n", pbuf->size);
}

static void packetEdit(buffer *pbuf, unsigned priv)
{
    packetDump(pbuf, "## Before packet edit(original data)");
    switch (priv) {
        case VLAN_OAM2DHCP:   setDhcpVlan(pbuf); break;
        case VLAN_DHCP2OAM:   setOamVlan(pbuf);  break;
        default: break;
    }
    packetDump(pbuf, "## After packet edit(data will be forward)");
}

static void setDhcpVlan(buffer *pbuf)
{
    unsigned short dhcpVlan=0;
    unsigned short pbit=0;
    unsigned short oamVlan=0;
    unsigned short tag = *((unsigned short *)&(pbuf->data[0xC]));
    tag = ntohs(tag);
    if (tag != 0x8100) {
        DHCP_FILTER_LOG(LOG_DEBUG, "untagged package detected, supprise!");
        return;
    }

    oamVlan = *((unsigned short *)&(pbuf->data[0xE]));
    oamVlan = ntohs(oamVlan);
    pbit = oamVlan&0xF000;
    oamVlan &= 0xFFF;

    dhcpVlan = oamVlan-1;
    if(dhcpVlan == 0)
        dhcpVlan = 4093;

    *((unsigned short *)&(pbuf->data[0xE])) = htons(pbit | dhcpVlan);
}

static void setOamVlan(buffer *pbuf)
{
    unsigned short oamVlan=0;
    unsigned short pbit=0;
    unsigned short dhcpVlan;
    unsigned short tag = *((unsigned short *)&(pbuf->data[0xC]));
    tag = ntohs(tag);
    if (tag != 0x8100) {
        DHCP_FILTER_LOG(LOG_DEBUG, "untagged package detected, supprise!");
        return;
    }

    dhcpVlan = *((unsigned short *)&(pbuf->data[0xE]));
    dhcpVlan = ntohs(dhcpVlan);
    pbit = dhcpVlan&0xF000;
    dhcpVlan &= 0xFFF;

    oamVlan = dhcpVlan+1;
    if(oamVlan == 4094)
        oamVlan = 1;

    *((unsigned short *)&(pbuf->data[0xE])) = htons(pbit | oamVlan);
}

static int createRawSocketAndBindToInterface(struct relay_info *info)
{
    char* itfName = info->itf;
    int type = info->type;
    int raw_socket_fd;
    DHCP_FILTER_LOG(LOG_INFO, "creating the socket for %s\n", itfName);
    if ((raw_socket_fd = socket(AF_PACKET, type, htons(ETH_P_ALL))) == -1) {
        DHCP_FILTER_LOG(LOG_ERR, "Impossible to create the socket for %s: %s\n", itfName, strerror(errno));
        return -1;
    }
    DHCP_FILTER_LOG(LOG_INFO, "The socket for %s is %d\n", itfName, raw_socket_fd);

    struct sockaddr_ll sckaddr = {};
    if (!getLLsocketOf(raw_socket_fd, itfName, &sckaddr)) {
        DHCP_FILTER_LOG(LOG_ERR, "Couldn't get the low-level socket of %s\n", itfName);
        return -1;
    }
    DHCP_FILTER_LOG(LOG_INFO, "Binding socket %d to %s\n", raw_socket_fd, itfName);
    if (bind(raw_socket_fd, (struct sockaddr *) &sckaddr, sizeof (sckaddr)) < 0) {
        DHCP_FILTER_LOG(LOG_ERR, "Couldn't attach to interface %s: %s\n", itfName, strerror(errno));
        return -1;
    }
    return raw_socket_fd;
}

static void attachItfFilterForService(int itf)
{
    attachItfFilter(itf, packetFilter, packetFilterSize);
}

static struct relay_info dhcp_info = {
    .id = RELAY_ID_DHCP,
    .name = "DHCP",
    .ops = {
        .parse_cmdline = NULL,
        .attach_filter = attachItfFilterForService,
        .create_network_interface = createRawSocketAndBindToInterface,
        .create_service_interface = createRawSocketAndBindToInterface,
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
    int ret = relay_register(&dhcp_info);
    if (ret)
        DHCP_FILTER_LOG(LOG_INFO, "register DHCP relay fail\n");
    return ret;
}
