/*
 * nflog_listen reads packets the firewall logged, over NETLINK_NETFILTER rather than
 * an AF_PACKET capture socket, the way ulogd does: the ruleset decides what is
 * interesting.  Binding a group needs CAP_NET_ADMIN, and nothing arrives until a
 * rule feeds it, which changes the host firewall.  The bind dies with the socket;
 * the rule does not.  Do both in a throwaway VM:
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
#include <net/if.h>
#include <netinet/in.h>

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_log.h>
#include <linux/netlink.h>

#include "common/log.h"
#include "common/net_util.h"
#include "netlink/nl_util.h"

#define DEFAULT_GROUP_HELPER 5
#define COPY_RANGE_HELPER 4096
#define IPV4_MIN_HDR_HELPER 20

union nflog_config {
    struct nlmsghdr nl_header;
    char raw[NLMSG_SPACE(sizeof(struct nfgenmsg)) + RTA_SPACE(sizeof(struct nfulnl_msg_config_mode))];
};

static int nfl_config_helper(int fd, unsigned short group, unsigned short attr, const void *data, unsigned short len)
{
    union nflog_config request;
    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    struct nfgenmsg *nf_header;
    struct nlmsghdr *nl_header;
    unsigned int seq;
    ssize_t received_bytes;
    int remaining;

    memset(&request, 0, sizeof(request));

    seq = nl_next_seq_helper();
    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(*nf_header));
    request.nl_header.nlmsg_type = (NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_CONFIG;
    request.nl_header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    request.nl_header.nlmsg_seq = seq;

    nf_header = (struct nfgenmsg *)NLMSG_DATA(&request.nl_header);
    nf_header->nfgen_family = AF_INET;
    nf_header->version = NFNETLINK_V0;
    nf_header->res_id = htons(group);

    if (nl_add_attr_helper(&request.nl_header, sizeof(request), attr, data, len) < 0) {
        return -1;
    }
    if (nl_send_helper(fd, &request, request.nl_header.nlmsg_len) < 0) {
        return -1;
    }

    while (1) {
        received_bytes = nl_recv_or_shutdown_helper(fd, nl_recv_buf, sizeof(nl_recv_buf), NULL);
        if (received_bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        remaining = (int)received_bytes;
        nl_header = (struct nlmsghdr *)nl_recv_buf;

        while (NLMSG_OK(nl_header, remaining)) {
            if (nl_header->nlmsg_seq == seq && nl_header->nlmsg_type == NLMSG_ERROR) {
                return nl_check_error_helper(nl_header);
            }
            nl_header = NLMSG_NEXT(nl_header, remaining);
        }
    }
}

static void format_ifname_helper(const struct rtattr *attr, char *out, size_t len)
{
    char name[IF_NAMESIZE] = { 0 };
    unsigned int ifindex;

    if (attr == NULL || RTA_PAYLOAD(attr) != sizeof(ifindex)) {
        snprintf(out, len, "-");
        return;
    }

    memcpy(&ifindex, RTA_DATA(attr), sizeof(ifindex));
    ifindex = ntohl(ifindex);
    if (if_indextoname(ifindex, name) == NULL) {
        snprintf(out, len, "%u", ifindex);
        return;
    }
    snprintf(out, len, "%s", name);
}

static void print_packet_helper(struct nlmsghdr *nl_header)
{
    char src_text[INET_ADDRSTRLEN] = { 0 };
    char dst_text[INET_ADDRSTRLEN] = { 0 };
    char in_ifname[IF_NAMESIZE] = { 0 };
    char out_ifname[IF_NAMESIZE] = { 0 };
    struct rtattr *attr_table[NFULA_MAX + 1];
    struct nfulnl_msg_packet_hdr *packet_header;
    const unsigned char *payload;
    int captured_len;

    nl_parse_attrs_helper((struct rtattr *)((char *)NLMSG_DATA(nl_header) + NLMSG_ALIGN(sizeof(struct nfgenmsg))),
                          (int)NLMSG_PAYLOAD(nl_header, sizeof(struct nfgenmsg)), attr_table, NFULA_MAX);

    if (attr_table[NFULA_PACKET_HDR] == NULL || attr_table[NFULA_PAYLOAD] == NULL) {
        return;
    }

    packet_header = (struct nfulnl_msg_packet_hdr *)RTA_DATA(attr_table[NFULA_PACKET_HDR]);
    payload = (const unsigned char *)RTA_DATA(attr_table[NFULA_PAYLOAD]);
    captured_len = (int)RTA_PAYLOAD(attr_table[NFULA_PAYLOAD]);

    format_ifname_helper(attr_table[NFULA_IFINDEX_INDEV], in_ifname, sizeof(in_ifname));
    format_ifname_helper(attr_table[NFULA_IFINDEX_OUTDEV], out_ifname, sizeof(out_ifname));

    if (ntohs(packet_header->hw_protocol) != ETH_P_IP || captured_len < IPV4_MIN_HDR_HELPER) {
        LOG_RESULT_HELPER("hook %u proto 0x%04x in %s out %s %d bytes", packet_header->hook,
                          ntohs(packet_header->hw_protocol), in_ifname, out_ifname, captured_len);
        return;
    }

    if (inet_ntop(AF_INET, payload + 12, src_text, sizeof(src_text)) == NULL) {
        return;
    }
    if (inet_ntop(AF_INET, payload + 16, dst_text, sizeof(dst_text)) == NULL) {
        return;
    }

    LOG_RESULT_HELPER("hook %u in %s out %s ipproto %u %s -> %s %d bytes", packet_header->hook, in_ifname, out_ifname,
                      payload[9], src_text, dst_text, captured_len);
}

int main(int argc, char **argv)
{
    struct nfulnl_msg_config_mode mode = { 0 };
    struct nfulnl_msg_config_cmd cmd = { 0 };
    unsigned short group = DEFAULT_GROUP_HELPER;
    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    struct nlmsghdr *nl_header;
    sigset_t wait_signal_mask;
    unsigned long parsed_group;
    char *endptr;
    ssize_t received_bytes;
    int remaining;
    int fd;

    if (argc > 1) {
        errno = 0;
        parsed_group = strtoul(argv[1], &endptr, 10);
        if (errno != 0 || endptr == argv[1] || *endptr != '\0' || parsed_group > 0xffff) {
            fprintf(stderr, "usage: %s [group]\n", argv[0]);
            return 1;
        }
        group = (unsigned short)parsed_group;
    }

    if (catch_shutdown_signals_helper(&wait_signal_mask) < 0) {
        LOG_ERRNO_HELPER("catch_shutdown_signals_helper()");
        return 1;
    }

    fd = nl_open_helper(NETLINK_NETFILTER);
    if (fd < 0) {
        LOG_ERRNO_HELPER("nl_open_helper(NETLINK_NETFILTER)");
        return 1;
    }

    cmd.command = NFULNL_CFG_CMD_BIND;
    if (nfl_config_helper(fd, group, NFULA_CFG_CMD, &cmd, sizeof(cmd)) < 0) {
        LOG_ERRNO_HELPER("NFULNL_CFG_CMD_BIND");
        close(fd);
        return 1;
    }

    mode.copy_mode = NFULNL_COPY_PACKET;
    mode.copy_range = htonl(COPY_RANGE_HELPER);
    if (nfl_config_helper(fd, group, NFULA_CFG_MODE, &mode, sizeof(mode)) < 0) {
        LOG_ERRNO_HELPER("NFULNL_CFG_MODE");
        close(fd);
        return 1;
    }

    LOG_MSG_HELPER("bound to nflog group %u", group);

    while (!should_shut_down_helper()) {
        received_bytes = nl_recv_or_shutdown_helper(fd, nl_recv_buf, sizeof(nl_recv_buf), &wait_signal_mask);
        if (received_bytes < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ENOBUFS) {
                LOG_MSG_HELPER("the kernel dropped packets, this socket overflowed");
                continue;
            }
            LOG_ERRNO_HELPER("nl_recv_helper()");
            close(fd);
            return 1;
        }

        remaining = (int)received_bytes;
        nl_header = (struct nlmsghdr *)nl_recv_buf;

        while (NLMSG_OK(nl_header, remaining)) {
            if (NFNL_MSG_TYPE(nl_header->nlmsg_type) == NFULNL_MSG_PACKET) {
                print_packet_helper(nl_header);
            }
            nl_header = NLMSG_NEXT(nl_header, remaining);
        }
    }

    LOG_MSG_HELPER("stopped by signal %d", which_shutdown_signal_helper());

    close(fd);
    return 0;
}