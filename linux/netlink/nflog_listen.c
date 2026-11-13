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

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_log.h>
#include <linux/netlink.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/log.h"
#include "common/net_util.h"
#include "netlink/nl_util.h"

#define DEFAULT_GROUP_HELPER 5
#define COPY_RANGE_HELPER 4096
#define IPV4_MIN_HDR_HELPER 20

union nflog_config {
    struct nlmsghdr nl_header;
    char raw[NLMSG_SPACE(sizeof(struct nfgenmsg)) +
             RTA_SPACE(sizeof(struct nfulnl_msg_config_mode))];
};

static int nfl_configure_helper(int fd, unsigned short group, unsigned short attr,
                                const void *const data, unsigned short len)
{
    union nflog_config request;

    memset(&request, 0, sizeof(request));

    unsigned int seq = nl_next_seq_helper();
    struct nfgenmsg *nf_header = NLMSG_DATA(&request.nl_header);

    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(*nf_header));
    request.nl_header.nlmsg_type = (NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_CONFIG;
    request.nl_header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    request.nl_header.nlmsg_seq = seq;

    nf_header->nfgen_family = AF_INET;
    nf_header->version = NFNETLINK_V0;
    nf_header->res_id = htons(group);

    if (nl_add_attr_helper(&request.nl_header, sizeof(request), attr, data, len) < 0) {
        return -1;
    }
    if (nl_send_helper(fd, &request, request.nl_header.nlmsg_len) < 0) {
        return -1;
    }

    char nl_recv_buf[NL_BUF_SIZE_HELPER];

    for (;;) {
        ssize_t received_bytes = nl_recv_or_shutdown_helper(fd, nl_recv_buf,
                                                            sizeof(nl_recv_buf), NULL);
        if (received_bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        int remaining = (int)received_bytes;

        for (const struct nlmsghdr *nl_header = (const struct nlmsghdr *)nl_recv_buf;
             NLMSG_OK(nl_header, remaining); nl_header = NLMSG_NEXT(nl_header, remaining)) {
            if (nl_header->nlmsg_seq == seq && nl_header->nlmsg_type == NLMSG_ERROR) {
                return nl_check_error_helper(nl_header);
            }
        }
    }
}

static void format_ifname_helper(char *const mut_out, size_t len,
                                 const struct rtattr *const attr_nullable)
{
    unsigned int ifindex;

    if (attr_nullable == NULL || RTA_PAYLOAD(attr_nullable) != sizeof(ifindex)) {
        snprintf(mut_out, len, "-");
        return;
    }

    memcpy(&ifindex, RTA_DATA(attr_nullable), sizeof(ifindex));
    ifindex = ntohl(ifindex);

    char name[IF_NAMESIZE] = { 0 };

    if (if_indextoname(ifindex, name) == NULL) {
        snprintf(mut_out, len, "%u", ifindex);
        return;
    }
    snprintf(mut_out, len, "%s", name);
}

static void print_packet_helper(const struct nlmsghdr *const nl_header)
{
    const char *message_data = NLMSG_DATA(nl_header);
    const struct rtattr *attrs = (const struct rtattr *)(message_data +
                                                         NLMSG_ALIGN(sizeof(struct nfgenmsg)));
    struct rtattr *attr_table[NFULA_MAX + 1];

    nl_parse_attrs_helper(attrs, (int)NLMSG_PAYLOAD(nl_header, sizeof(struct nfgenmsg)),
                          attr_table, NFULA_MAX);

    if (attr_table[NFULA_PACKET_HDR] == NULL || attr_table[NFULA_PAYLOAD] == NULL) {
        return;
    }

    const struct nfulnl_msg_packet_hdr *packet_header = RTA_DATA(attr_table[NFULA_PACKET_HDR]);
    const unsigned char *payload = RTA_DATA(attr_table[NFULA_PAYLOAD]);
    int captured_len = (int)RTA_PAYLOAD(attr_table[NFULA_PAYLOAD]);
    char in_ifname[IF_NAMESIZE] = { 0 };
    char out_ifname[IF_NAMESIZE] = { 0 };

    format_ifname_helper(in_ifname, sizeof(in_ifname), attr_table[NFULA_IFINDEX_INDEV]);
    format_ifname_helper(out_ifname, sizeof(out_ifname), attr_table[NFULA_IFINDEX_OUTDEV]);

    if (ntohs(packet_header->hw_protocol) != ETH_P_IP || captured_len < IPV4_MIN_HDR_HELPER) {
        LOG_RESULT_HELPER("hook %u proto 0x%04x in %s out %s %d bytes", packet_header->hook,
                          ntohs(packet_header->hw_protocol), in_ifname, out_ifname, captured_len);
        return;
    }

    char src_text[INET_ADDRSTRLEN] = { 0 };
    char dst_text[INET_ADDRSTRLEN] = { 0 };

    if (inet_ntop(AF_INET, payload + 12, src_text, sizeof(src_text)) == NULL) {
        return;
    }
    if (inet_ntop(AF_INET, payload + 16, dst_text, sizeof(dst_text)) == NULL) {
        return;
    }

    LOG_RESULT_HELPER("hook %u in %s out %s ipproto %u %s -> %s %d bytes", packet_header->hook,
                      in_ifname, out_ifname, payload[9], src_text, dst_text, captured_len);
}

int main(int argc, char *argv[])
{
    unsigned short group = DEFAULT_GROUP_HELPER;

    if (argc > 1) {
        char *endptr = NULL;

        errno = 0;
        unsigned long parsed_group = strtoul(argv[1], &endptr, 10);
        if (errno != 0 || endptr == argv[1] || *endptr != '\0' || parsed_group > 0xFFFF) {
            fprintf(stderr, "usage: %s [group]\n", argv[0]);
            return EXIT_FAILURE;
        }
        group = (unsigned short)parsed_group;
    }

    sigset_t wait_signal_mask;

    if (catch_shutdown_signals_helper(&wait_signal_mask) < 0) {
        LOG_ERRNO_HELPER("catch_shutdown_signals_helper()");
        return EXIT_FAILURE;
    }

    int fd = nl_open_helper(NETLINK_NETFILTER);
    if (fd < 0) {
        LOG_ERRNO_HELPER("nl_open_helper(NETLINK_NETFILTER)");
        return EXIT_FAILURE;
    }

    int exit_status = EXIT_FAILURE;
    struct nfulnl_msg_config_cmd cmd = {
        .command = NFULNL_CFG_CMD_BIND
    };

    if (nfl_configure_helper(fd, group, NFULA_CFG_CMD, &cmd, sizeof(cmd)) < 0) {
        LOG_ERRNO_HELPER("NFULNL_CFG_CMD_BIND");
        goto out_close_fd;
    }

    struct nfulnl_msg_config_mode mode = {
        .copy_range = htonl(COPY_RANGE_HELPER),
        .copy_mode = NFULNL_COPY_PACKET
    };

    if (nfl_configure_helper(fd, group, NFULA_CFG_MODE, &mode, sizeof(mode)) < 0) {
        LOG_ERRNO_HELPER("NFULNL_CFG_MODE");
        goto out_close_fd;
    }

    LOG_MSG_HELPER("bound to nflog group %u", group);

    char nl_recv_buf[NL_BUF_SIZE_HELPER];

    while (should_shut_down_helper() == 0) {
        ssize_t received_bytes = nl_recv_or_shutdown_helper(fd, nl_recv_buf, sizeof(nl_recv_buf),
                                                            &wait_signal_mask);
        if (received_bytes < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ENOBUFS) {
                LOG_MSG_HELPER("the kernel dropped packets, this socket overflowed");
                continue;
            }
            LOG_ERRNO_HELPER("nl_recv_helper()");
            goto out_close_fd;
        }

        int remaining = (int)received_bytes;

        for (const struct nlmsghdr *nl_header = (const struct nlmsghdr *)nl_recv_buf;
             NLMSG_OK(nl_header, remaining); nl_header = NLMSG_NEXT(nl_header, remaining)) {
            if (NFNL_MSG_TYPE(nl_header->nlmsg_type) == NFULNL_MSG_PACKET) {
                print_packet_helper(nl_header);
            }
        }
    }

    LOG_MSG_HELPER("stopped by signal %d", get_shutdown_signal_helper());
    exit_status = 0;

out_close_fd:
    close(fd);
    return exit_status;
}
