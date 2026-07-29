/*
 * nflog_listen receives copies of packets the firewall logged, over
 * NETLINK_NETFILTER instead of an AF_PACKET capture socket.  It is the path ulogd
 * and similar tools take: the kernel decides which packets are interesting and the
 * program only reads what the ruleset already selected.
 *
 * Binding a log group needs CAP_NET_ADMIN, and nothing arrives until a rule feeds
 * the group, which changes the host firewall.  Do both in a throwaway VM:
 *
 *     iptables -A OUTPUT -p icmp -j NFLOG --nflog-group 5
 *     ./linux/bin/netlink/nflog_listen 5
 */

#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <netinet/in.h>

#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_log.h>
#include <linux/netlink.h>

#include "common/log.h"
#include "common/nl_util.h"

#define DEFAULT_GROUP 5
#define COPY_RANGE 0xffff
#define IPV4_MIN_HDR 20

static volatile sig_atomic_t g_stop;

static void on_stop(int signo)
{
    (void)signo;
    g_stop = 1;
}

static int install_stop_handler(int signo)
{
    struct sigaction sa = { 0 };

    sa.sa_handler = on_stop;
    if (sigemptyset(&sa.sa_mask) < 0) {
        return -1;
    }
    return sigaction(signo, &sa, NULL);
}

static int nfl_config(int fd, unsigned short group, unsigned short attr, const void *data, unsigned short len)
{
    union {
        struct nlmsghdr nlh;
        char raw[NLMSG_SPACE(sizeof(struct nfgenmsg)) + RTA_SPACE(sizeof(struct nfulnl_msg_config_mode))];
    } req;
    char buf[NL_BUF_SIZE];
    struct nfgenmsg *nfg;
    struct nlmsghdr *nlh;
    unsigned int seq;
    ssize_t n;
    int left;

    memset(&req, 0, sizeof(req));

    seq = nl_next_seq();
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(*nfg));
    req.nlh.nlmsg_type = (NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_CONFIG;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    req.nlh.nlmsg_seq = seq;

    nfg = (struct nfgenmsg *)NLMSG_DATA(&req.nlh);
    nfg->nfgen_family = AF_INET;
    nfg->version = NFNETLINK_V0;
    nfg->res_id = htons(group);

    if (nl_add_attr(&req.nlh, sizeof(req), attr, data, len) < 0) {
        return -1;
    }
    if (nl_send(fd, &req, req.nlh.nlmsg_len) < 0) {
        return -1;
    }

    while (1) {
        n = nl_recv(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        left = (int)n;
        nlh = (struct nlmsghdr *)buf;

        while (NLMSG_OK(nlh, left)) {
            if (nlh->nlmsg_seq == seq && nlh->nlmsg_type == NLMSG_ERROR) {
                return nl_check_error(nlh);
            }
            nlh = NLMSG_NEXT(nlh, left);
        }
    }
}

static void print_packet(struct nlmsghdr *nlh)
{
    char src[INET_ADDRSTRLEN] = { 0 };
    char dst[INET_ADDRSTRLEN] = { 0 };
    struct rtattr *tb[NFULA_MAX + 1];
    struct nfulnl_msg_packet_hdr *ph;
    const unsigned char *payload;
    unsigned int indev = 0;
    int caplen;

    nl_parse_attrs((struct rtattr *)((char *)NLMSG_DATA(nlh) + NLMSG_ALIGN(sizeof(struct nfgenmsg))),
                   (int)NLMSG_PAYLOAD(nlh, sizeof(struct nfgenmsg)), tb, NFULA_MAX);

    if (tb[NFULA_PACKET_HDR] == NULL || tb[NFULA_PAYLOAD] == NULL) {
        return;
    }

    ph = (struct nfulnl_msg_packet_hdr *)RTA_DATA(tb[NFULA_PACKET_HDR]);
    payload = (const unsigned char *)RTA_DATA(tb[NFULA_PAYLOAD]);
    caplen = (int)RTA_PAYLOAD(tb[NFULA_PAYLOAD]);

    if (tb[NFULA_IFINDEX_INDEV] != NULL) {
        memcpy(&indev, RTA_DATA(tb[NFULA_IFINDEX_INDEV]), sizeof(indev));
        indev = ntohl(indev);
    }

    if (ntohs(ph->hw_protocol) != ETH_P_IP || caplen < IPV4_MIN_HDR) {
        printf("nflog_listen: hook %u proto 0x%04x indev %u %d bytes\n", ph->hook, ntohs(ph->hw_protocol), indev, caplen);
        return;
    }

    if (inet_ntop(AF_INET, payload + 12, src, sizeof(src)) == NULL) {
        return;
    }
    if (inet_ntop(AF_INET, payload + 16, dst, sizeof(dst)) == NULL) {
        return;
    }

    printf("nflog_listen: hook %u indev %u ipproto %u %s -> %s %d bytes\n", ph->hook, indev, payload[9], src, dst, caplen);
}

int main(int argc, char **argv)
{
    struct nfulnl_msg_config_mode mode = { 0 };
    struct nfulnl_msg_config_cmd cmd = { 0 };
    unsigned short group = DEFAULT_GROUP;
    char buf[NL_BUF_SIZE];
    struct nlmsghdr *nlh;
    unsigned long parsed;
    char *endptr;
    ssize_t n;
    int left;
    int fd;

    if (argc > 1) {
        errno = 0;
        parsed = strtoul(argv[1], &endptr, 10);
        if (errno != 0 || endptr == argv[1] || *endptr != '\0' || parsed > 0xffff) {
            fprintf(stderr, "usage: %s [group]\n", argv[0]);
            return 1;
        }
        group = (unsigned short)parsed;
    }

    if (install_stop_handler(SIGINT) < 0 || install_stop_handler(SIGTERM) < 0) {
        log_errno("sigaction()");
        return 1;
    }

    fd = nl_open(NETLINK_NETFILTER);
    if (fd < 0) {
        log_errno("nl_open(NETLINK_NETFILTER)");
        return 1;
    }

    cmd.command = NFULNL_CFG_CMD_PF_BIND;
    if (nfl_config(fd, 0, NFULA_CFG_CMD, &cmd, sizeof(cmd)) < 0) {
        log_errno("NFULNL_CFG_CMD_PF_BIND");
        close(fd);
        return 1;
    }

    cmd.command = NFULNL_CFG_CMD_BIND;
    if (nfl_config(fd, group, NFULA_CFG_CMD, &cmd, sizeof(cmd)) < 0) {
        log_errno("NFULNL_CFG_CMD_BIND");
        close(fd);
        return 1;
    }

    mode.copy_mode = NFULNL_COPY_PACKET;
    mode.copy_range = htonl(COPY_RANGE);
    if (nfl_config(fd, group, NFULA_CFG_MODE, &mode, sizeof(mode)) < 0) {
        log_errno("NFULNL_CFG_MODE");
        close(fd);
        return 1;
    }

    log_msg("bound to nflog group %u", group);

    while (!g_stop) {
        n = nl_recv(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_errno("nl_recv()");
            close(fd);
            return 1;
        }

        left = (int)n;
        nlh = (struct nlmsghdr *)buf;

        while (NLMSG_OK(nlh, left)) {
            if (NFNL_MSG_TYPE(nlh->nlmsg_type) == NFULNL_MSG_PACKET) {
                print_packet(nlh);
            }
            nlh = NLMSG_NEXT(nlh, left);
        }
        fflush(stdout);
    }

    log_msg("stopped");

    close(fd);
    return 0;
}