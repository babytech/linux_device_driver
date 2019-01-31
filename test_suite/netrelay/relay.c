/*
   Copyright (c) 2016 Nokia. All rights reserved.
   This program contains proprietary information which is a trade secret
   of Nokia and also is protected as an unpublished work under applicable
   Copyright laws. Recipient is to retain this program in confidence and
   is not permitted to use or make copies thereof other than as permitted
   in a written agreement with Nokia.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
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

extern char *optarg;
extern int optind, opterr, optopt;

static int parseArgs(int argc, char *argv[], progArgs *theArgs);
static int createThread(threadArgs *theArgs);
static void *forward_packet_from_to(void *thArgs);

#define RELAY_FWD_LOG(priority,format,...) myLogFun(__FILE__, __LINE__,__func__, priority, format, ##__VA_ARGS__)
#define SERVICE_PRIV(serviceEnabled,opCode) ((serviceEnabled) ? (opCode) : 0)
#define MAX_LOG_LEVEL 2
#define MAX_LOG_BUF_SIZE 255

static unsigned logLevel = 0;
static struct relay_info *relay_list[_RELAY_ID_MAX] = { NULL };
static unsigned syslogEnabled = 0;
static unsigned serviceEnabled = 0;
static unsigned filterEnable = 0;
static enum relay_id relayId;

const static char *usage = "\n\nusage: %s -n <network_itf> -t <service_itf> [-s -d -e -E -F]\n"
"-n: network interface to use\n"
"-t: service interface to use\n"
"-s: use syslog for iso stdout for logging\n"
"-E: enable Eoe packet header\n"
"-V: enable dummy dhcp vlan\n"
"-F: enable packets filters on interface\n"
"-d[d]: Debug info. -dd is A lot more of output!\n\n";

int service_init(void) __attribute__ ((weak, alias("_dummy_service_init")));

static int _dummy_service_init(void)
{
    printf("%s : this is a stub function\r\n", __func__);
    return -1;
}

int main(int argc, char *argv[])
{
    progArgs theArgs;
    printf("Starting forwarder application: %s\n", argv[0]);
    if (!parseArgs(argc, argv, &theArgs)) {
        const char *delim = "/";
        char *strToParse = argv[0];
        char *progTok = NULL;
        char *tempTok;
        char *save;
        while ((tempTok = strtok_r(strToParse, delim, &save)) != NULL) {
            progTok = tempTok;
            strToParse = NULL;
        }
        RELAY_FWD_LOG(LOG_ERR, usage, progTok == NULL ? argv[0] : progTok);
        return EXIT_FAILURE;
    }

    if (service_init())
        return EXIT_FAILURE;

    int net_fd, service_fd;
    char *netItf = theArgs.netItf;
    char *serviceItf = theArgs.serviceItf;
    unsigned int priv;

    RELAY_FWD_LOG(LOG_INFO, "Argument are: netItf: %s, serviceItf: %s\n", netItf, serviceItf);

    struct relay_info *info = relay_find(relayId);
    if (!info) {
        RELAY_FWD_LOG(LOG_ERR, "relay_find:failed, info = NULL!\n");
        return EXIT_FAILURE;
    }

    struct custom_handle_func *ops = &info->ops;
    info->itf = netItf;
    info->type = SOCK_RAW;
    if ((net_fd = ops->create_network_interface(info)) == -1) {
        RELAY_FWD_LOG(LOG_ERR, "Couldn't create raw socket for %s\n", netItf);
        exit(EXIT_FAILURE);
    }

    if (filterEnable)
        ops->attach_filter(net_fd);

    info->itf = serviceItf;
    info->type = SOCK_RAW;
    if ((service_fd = ops->create_service_interface(info)) == -1) {
        RELAY_FWD_LOG(LOG_ERR, "Couldn't create raw socket for %s\n", serviceItf);
        exit(EXIT_FAILURE);
    }

    priv = SERVICE_PRIV(serviceEnabled, SERVICE_TO_NET);
    threadArgs serviceToNet = { service_fd, net_fd, "service to net", priv };

    if (!createThread(&serviceToNet)) {
        RELAY_FWD_LOG(LOG_ERR, "Couldn't create the thread\n");
        return EXIT_FAILURE;
    }

    RELAY_FWD_LOG(LOG_INFO, "net_fd is %d and service_fd is %d\n", net_fd, service_fd);

    priv = SERVICE_PRIV(serviceEnabled, NET_TO_SERVICE);
    threadArgs netToService = { net_fd, service_fd, "net to service", priv };

    forward_packet_from_to(&netToService);
    return EXIT_FAILURE;
}

void *forward_packet_from_to(void* thArgs)
{
    threadArgs *theArgs = (threadArgs *) thArgs;
    int from_fd = theArgs->from_fd;
    int to_fd = theArgs->to_fd;
    char *tName = theArgs->name;
    unsigned priv = theArgs->priv;
    buffer *packetBuf;
    ssize_t nbByteRead;
    struct relay_info *info = relay_find(relayId);
    struct custom_packet_func *pkg = &info->pkg;

    if (!pkg->bufferInit || !pkg->bufferPrepare
            || !pkg->dump || !pkg->edit)
        exit(EXIT_FAILURE);

    if (pkg->bufferInit(&packetBuf, priv))
        exit(EXIT_FAILURE);

    RELAY_FWD_LOG(LOG_INFO, "%s thread is waiting for data on %d to transmit on %d. Buffer size is %d\n",
        tName, from_fd, to_fd, packetBuf->allocated);

    while (1) {
        pkg->bufferPrepare(packetBuf, priv);
        //This read will return all the time a full packet
        if ((nbByteRead = read(from_fd, packetBuf->data, bufferDataToTail(packetBuf))) == -1) {
            switch (errno) {
                case EAGAIN:
                case EINTR:
                    continue;
                default:
                    RELAY_FWD_LOG(LOG_ERR, "%s had an error while reading on %d: %d (%s)!\n", tName, from_fd, errno, strerror(errno));
                    exit(EXIT_FAILURE);
            }
        }

        if (logLevel)
            RELAY_FWD_LOG(LOG_DEBUG, "%s received %d bytes to forward on %d.\n", tName, nbByteRead, to_fd);

        bufferSetDataSize(packetBuf, nbByteRead);
        pkg->edit(packetBuf, priv);

        ssize_t ret;
        ssize_t nbByteWritten = 0;
        unsigned int nbByteToWrite = packetBuf->size;
        while (nbByteToWrite != 0 && (ret = write(to_fd, packetBuf->data + nbByteWritten, nbByteToWrite)) != 0) {
            if (ret == -1) {
                switch (errno) {
                    case EAGAIN:
                    case EINTR:
                        continue;
                    default:
                        RELAY_FWD_LOG(LOG_ERR, "%s error while forwarding data: %d: %s\n", tName, errno, strerror(errno));
                        exit(EXIT_FAILURE);
                }
            }
            nbByteWritten += ret;
            nbByteToWrite -= ret;
            RELAY_FWD_LOG(LOG_DEBUG, "%s has forwarded %d bytes, %d remains\n", tName, ret, nbByteToWrite);
        }
        RELAY_FWD_LOG(LOG_DEBUG, "%s has finished to forward %d byte to %d\n", tName, nbByteWritten, to_fd);
    }
}

int getLLsocketOf(int socket_fd, char* itfName, struct sockaddr_ll * ll_sockaddr)
{
    RELAY_FWD_LOG(LOG_INFO, "Getting interface index of %s\n", itfName);
    struct ifreq ifr = {};
    snprintf(ifr.ifr_name, sizeof (ifr.ifr_name), "%s", itfName);
    if (ioctl(socket_fd, SIOCGIFINDEX, &ifr) == -1) {
        RELAY_FWD_LOG(LOG_ERR, "Couldn't get interface index of %s: %s\n", itfName, strerror(errno));
        return 0;
    }

    ll_sockaddr->sll_family = AF_PACKET;
    ll_sockaddr->sll_protocol = htons(ETH_P_ALL);
    ll_sockaddr->sll_ifindex = ifr.ifr_ifindex;
    ll_sockaddr->sll_halen = ETH_ALEN;
    if (!getMacAddressOf(socket_fd, itfName, ll_sockaddr->sll_addr)) {
        RELAY_FWD_LOG(LOG_ERR, "Couldn't get the MAC address of %s\n", itfName);
        return 0;
    }
    return 1;
}

int getMacAddressOf(int raw_socket_fd, char* itfName, unsigned char* macAddrTab)
{
    unsigned int i;
    struct ifreq ifr = {};
    snprintf(ifr.ifr_name, sizeof (ifr.ifr_name), "%s", itfName);
    if (ioctl(raw_socket_fd, SIOCGIFHWADDR, &ifr) == -1) {
        fprintf(stderr, "Couldn't get MAC address of %s: %s\n", itfName, strerror(errno));
        return 0;
    }
    for (i = 0; i < 6; i++) {
        if (logLevel > 1)
            RELAY_FWD_LOG(LOG_DEBUG, "MAC address of %s[%d] is %#x\n", itfName, i, ifr.ifr_hwaddr.sa_data[i]);
        macAddrTab[i] = ifr.ifr_hwaddr.sa_data[i];
    }
    return 1;
}

int createThread(threadArgs * theArgs)
{
    pthread_t theThread;
    int rc;
    RELAY_FWD_LOG(LOG_INFO, "creating thread %s\n", theArgs->name);
    if ((rc = pthread_create(&theThread, NULL, forward_packet_from_to, (void *) theArgs))) {
        RELAY_FWD_LOG(LOG_ERR, "ERROR; return code from pthread_create() is %d: %s\n", rc, strerror(rc));
        return 0;
    }
    return 1;
}

int parseArgs(int argc, char *argv[], progArgs * theArgs)
{
    int c;
    theArgs->netItf = NULL;
    theArgs->serviceItf = NULL;
    while ((c = getopt(argc, argv, "EVFdsn:t:")) != -1) {
        switch (c) {
            case 'n':
                theArgs->netItf = optarg;
                break;
            case 't':
                theArgs->serviceItf = optarg;
                break;
            case 's':
                openlog(NULL, LOG_CONS | LOG_NDELAY | LOG_PID, LOG_DAEMON);
                syslogEnabled = 1;
                break;
            case 'd':
                if (logLevel < MAX_LOG_LEVEL)
                    logLevel++;
                break;
            case 'E':
                serviceEnabled = 1;
                relayId = RELAY_ID_EOE;
                RELAY_FWD_LOG(LOG_INFO, "Enable EOE header handle.\n");
                break;
            case 'V':
                serviceEnabled = 1;
                relayId = RELAY_ID_DHCP;
                RELAY_FWD_LOG(LOG_INFO, "Enable dummy dhcp vlan.\n");
                break;
            case 'F':
                filterEnable = 1;
                RELAY_FWD_LOG(LOG_INFO, "Enable packets filter.\n");
                break;
            case '?':
                return 0;
            case ':':
                RELAY_FWD_LOG(LOG_ERR, "Require args\n");
                break;
            default:
                if (isprint(c)) {
                    RELAY_FWD_LOG(LOG_ERR, "Unknown option %c\n", c);
                } else {
                    RELAY_FWD_LOG(LOG_ERR, "Unknown option character %#x\n", c);
                }
                return 0;
        }
    }
    if (theArgs->netItf == NULL || theArgs->serviceItf == NULL) {
        RELAY_FWD_LOG(LOG_ERR, "Error: Some arguments are missing...\n");
        return 0;
    }
    return 1;
}

int setItfToPromiscModeAndDisableArpProtocol(int fd, char* itfName)
{
    struct ifreq eth = {};
    strncpy(eth.ifr_name, itfName, IFNAMSIZ);
    RELAY_FWD_LOG(LOG_INFO, "Setting %s to promiscuous mode and disable ARP resolution\n", itfName);
    if (ioctl(fd, SIOCGIFFLAGS, &eth) == -1) {
        RELAY_FWD_LOG(LOG_ERR, "Couldn't get the flags of %s. Errno %d %s", itfName, errno, strerror(errno));
        return 0;
    }
    eth.ifr_flags |= IFF_PROMISC;
    eth.ifr_flags |= IFF_NOARP;
    if (ioctl(fd, SIOCSIFFLAGS, &eth) == -1) {
        RELAY_FWD_LOG(LOG_ERR, "Couldn't set the flags of %s. Errno %d %s", itfName, errno, strerror(errno));
        return 0;
    }
    RELAY_FWD_LOG(LOG_INFO, "%s is now set in promiscuous mode and ARP resolution is disabled\n", itfName);
    return 1;
}

void myLogFun(char* fileName, int lineNum, const char *fun, int priority, const char* format, ...)
{
    if (priority == LOG_DEBUG && !logLevel)
        return;

    const unsigned int sizeOfLogBuf = strlen(format) + MAX_LOG_BUF_SIZE;
    char logBuf[sizeOfLogBuf];
    va_list args;
    va_start(args, format);
    vsnprintf(logBuf, sizeOfLogBuf, format, args);
    va_end(args);

    if (syslogEnabled) {
        syslog(priority, "(%s:%s:%d) %s", fileName, fun, lineNum, logBuf);
    } else {
        switch (priority) {
            case LOG_ERR:
                fprintf(stderr, "%s", logBuf);
                break;
            default:
                printf("%s", logBuf);
        }
    }
}

int bufferInit(buffer **pbuf, size_t size)
{
    buffer *buf;

    buf = malloc(sizeof(buffer));
    if (!buf)
        goto err;

    buf->base = malloc(size);
    if (!buf->base) {
        free(buf);
        goto err;
    }

    buf->allocated = size;
    *pbuf = buf;
    return bufferReset(*pbuf);
err:
    RELAY_FWD_LOG(LOG_ERR, "Init buffer fail(%d,%s)\n", errno, strerror(errno));
    return -1;
}

int bufferReset(buffer *pbuf)
{
    pbuf->data = pbuf->base;
    pbuf->size = 0;
    return 0;
}

size_t bufferDataToTail(buffer *pbuf)
{
    return pbuf->base + pbuf->allocated - pbuf->data;
}

void bufferSetDataSize(buffer *pbuf, size_t size)
{
    pbuf->size = size;
}

void attachItfFilter(int itf, struct sock_filter *filter_data, unsigned short filter_len)
{
    unsigned short i;
    struct sock_fprog Filter;

    Filter.len = filter_len;
    Filter.filter = filter_data;

    RELAY_FWD_LOG(LOG_INFO, "Attach filter to %d\n", itf);
    if (logLevel > 1) {
        RELAY_FWD_LOG(LOG_DEBUG, "Filter list:\n");
        for (i = 0; i < filter_len; i++) {
            RELAY_FWD_LOG(LOG_DEBUG, "{ 0x%02x, %d, %d, 0x%08lx }\n",
                    filter_data[i].code,
                    filter_data[i].jt,
                    filter_data[i].jf,
                    filter_data[i].k);
        }
        RELAY_FWD_LOG(LOG_DEBUG, "\n");
    }

    if(setsockopt(itf, SOL_SOCKET, SO_ATTACH_FILTER, (void *)&Filter, sizeof(Filter)) < 0) {
        RELAY_FWD_LOG(LOG_ERR, "Can not attach the filter, error: %d(%s)\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

struct relay_info *relay_find(enum relay_id id)
{
    if (id < 0 || id >= _RELAY_ID_MAX)
        return NULL;
    return relay_list[id];
}

int relay_register(struct relay_info *info)
{
    int ret = -1;

    do {
        if (!info || info->id >= _RELAY_ID_MAX) {
            RELAY_FWD_LOG(LOG_ERR, "invalid arguments\n");
            break;
        }

        if (relay_list[info->id]) {
            RELAY_FWD_LOG(LOG_ERR, "%s has register the same system\n", relay_list[info->id]->name);
            break;
        }

        relay_list[info->id] = info;
        RELAY_FWD_LOG(LOG_ERR, "register %s relay done\n", info->name);

        ret = 0;
    } while (0);

    return ret;
}

int get_logLevel(void)
{
    return logLevel;
}
