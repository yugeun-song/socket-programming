/*
 * addr_manage writes to the kernel: RTM_NEWADDR and RTM_DELADDR add and remove an
 * IPv4 address on a live interface, which needs CAP_NET_ADMIN and takes effect
 * immediately on the running system.  Run add/del inside a throwaway VM, not on a
 * host whose networking matters:
 *
 *     vng --run <bzImage> -- ./linux/bin/netlink/addr_manage add lo 127.9.9.9/32
 *
 * The list subcommand, and every other program under netlink/, only reads.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#include <linux/if_addr.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "common/log.h"
#include "netlink/nl_util.h"

struct addr_request {
    struct nlmsghdr nl_header;
    struct ifaddrmsg addr_info;
};

union addr_change {
    struct nlmsghdr nl_header;
    char raw[NLMSG_SPACE(sizeof(struct ifaddrmsg)) + 2 * RTA_SPACE(sizeof(struct in_addr))];
};

static void usage_helper(const char *argv0)
{
    fprintf(stderr, "usage: %s list\n", argv0);
    fprintf(stderr, "       %s add <ifname> <addr>/<prefix>\n", argv0);
    fprintf(stderr, "       %s del <ifname> <addr>/<prefix>\n", argv0);
}

static int parse_cidr_helper(const char *cidr_text, struct in_addr *ip, unsigned char *prefix)
{
    char host[INET_ADDRSTRLEN] = { 0 };
    const char *slash_at;
    unsigned long prefix_bits;
    char *endptr;
    size_t len;

    slash_at = strchr(cidr_text, '/');
    if (slash_at == NULL) {
        errno = EINVAL;
        return -1;
    }

    len = (size_t)(slash_at - cidr_text);
    if (len == 0 || len >= sizeof(host)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(host, cidr_text, len);

    if (inet_pton(AF_INET, host, ip) != 1) {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    prefix_bits = strtoul(slash_at + 1, &endptr, 10);
    if (errno != 0 || endptr == slash_at + 1 || *endptr != '\0' || prefix_bits > 32) {
        errno = EINVAL;
        return -1;
    }

    *prefix = (unsigned char)prefix_bits;
    return 0;
}

static void print_addr_helper(struct nlmsghdr *nl_header)
{
    char addr_text[INET6_ADDRSTRLEN] = { 0 };
    char name[IF_NAMESIZE] = { 0 };
    struct rtattr *attr_table[IFA_MAX + 1];
    struct ifaddrmsg *addr_info;
    struct rtattr *addr_attr;

    addr_info = (struct ifaddrmsg *)NLMSG_DATA(nl_header);
    nl_parse_attrs_helper(IFA_RTA(addr_info), (int)IFA_PAYLOAD(nl_header), attr_table, IFA_MAX);

    addr_attr = (attr_table[IFA_LOCAL] != NULL) ? attr_table[IFA_LOCAL] : attr_table[IFA_ADDRESS];
    if (addr_attr == NULL) {
        return;
    }
    if (inet_ntop(addr_info->ifa_family, RTA_DATA(addr_attr), addr_text, sizeof(addr_text)) == NULL) {
        return;
    }
    if (if_indextoname(addr_info->ifa_index, name) == NULL) {
        snprintf(name, sizeof(name), "%u", addr_info->ifa_index);
    }

    LOG_RESULT_HELPER("%-10s %s/%u", name, addr_text, addr_info->ifa_prefixlen);
}

static int list_addrs_helper(int fd)
{
    struct addr_request request = { 0 };
    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    struct nlmsghdr *nl_header;
    ssize_t received_bytes;
    unsigned int seq;
    int is_done = 0;
    int remaining;

    seq = nl_next_seq_helper();
    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(request.addr_info));
    request.nl_header.nlmsg_type = RTM_GETADDR;
    request.nl_header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.nl_header.nlmsg_seq = seq;
    request.addr_info.ifa_family = AF_UNSPEC;

    if (nl_send_helper(fd, &request, request.nl_header.nlmsg_len) < 0) {
        return -1;
    }

    while (!is_done) {
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
            if (nl_header->nlmsg_seq != seq) {
                nl_header = NLMSG_NEXT(nl_header, remaining);
                continue;
            }
            if (nl_header->nlmsg_type == NLMSG_DONE) {
                if (nl_check_done_helper(nl_header) < 0) {
                    return -1;
                }
                is_done = 1;
                break;
            }
            if (nl_check_error_helper(nl_header) < 0) {
                return -1;
            }
            if (nl_header->nlmsg_type == RTM_NEWADDR) {
                print_addr_helper(nl_header);
            }
            nl_header = NLMSG_NEXT(nl_header, remaining);
        }
    }

    return 0;
}

static int change_addr_helper(int fd, unsigned short msg_type, unsigned short extra_flags, unsigned int ifindex,
                              const struct in_addr *ip, unsigned char prefix)
{
    union addr_change request;
    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    struct ifaddrmsg *addr_info;
    struct nlmsghdr *nl_header;
    ssize_t received_bytes;
    unsigned int seq;
    int remaining;

    memset(&request, 0, sizeof(request));

    seq = nl_next_seq_helper();
    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(*addr_info));
    request.nl_header.nlmsg_type = msg_type;
    request.nl_header.nlmsg_flags = (unsigned short)(NLM_F_REQUEST | NLM_F_ACK | extra_flags);
    request.nl_header.nlmsg_seq = seq;

    addr_info = (struct ifaddrmsg *)NLMSG_DATA(&request.nl_header);
    addr_info->ifa_family = AF_INET;
    addr_info->ifa_prefixlen = prefix;
    addr_info->ifa_index = ifindex;
    addr_info->ifa_scope = RT_SCOPE_UNIVERSE;

    if (nl_add_attr_helper(&request.nl_header, sizeof(request), IFA_LOCAL, ip, sizeof(*ip)) < 0) {
        return -1;
    }
    if (nl_add_attr_helper(&request.nl_header, sizeof(request), IFA_ADDRESS, ip, sizeof(*ip)) < 0) {
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

int main(int argc, char **argv)
{
    unsigned short extra_flags = 0;
    unsigned short msg_type = 0;
    unsigned char prefix = 0;
    unsigned int ifindex = 0;
    struct in_addr ip = { 0 };
    int is_listing = 0;
    int exit_status;
    int fd;

    if (argc < 2) {
        usage_helper(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "list") == 0) {
        is_listing = 1;
    } else if (strcmp(argv[1], "add") == 0) {
        msg_type = RTM_NEWADDR;
        extra_flags = NLM_F_CREATE | NLM_F_EXCL;
    } else if (strcmp(argv[1], "del") == 0) {
        msg_type = RTM_DELADDR;
    } else {
        usage_helper(argv[0]);
        return 1;
    }

    if (!is_listing) {
        if (argc < 4) {
            usage_helper(argv[0]);
            return 1;
        }
        ifindex = if_nametoindex(argv[2]);
        if (ifindex == 0) {
            LOG_ERRNO_HELPER("if_nametoindex()");
            return 1;
        }
        if (parse_cidr_helper(argv[3], &ip, &prefix) < 0) {
            LOG_MSG_HELPER("bad address '%s', expected <addr>/<prefix>", argv[3]);
            return 1;
        }
    }

    fd = nl_open_helper(NETLINK_ROUTE);
    if (fd < 0) {
        LOG_ERRNO_HELPER("nl_open_helper()");
        return 1;
    }

    if (is_listing) {
        exit_status = list_addrs_helper(fd);
        if (exit_status < 0) {
            LOG_ERRNO_HELPER("RTM_GETADDR");
        }
    } else {
        exit_status = change_addr_helper(fd, msg_type, extra_flags, ifindex, &ip, prefix);
        if (exit_status < 0) {
            LOG_ERRNO_HELPER((msg_type == RTM_NEWADDR) ? "RTM_NEWADDR" : "RTM_DELADDR");
        } else {
            LOG_RESULT_HELPER("%s %s on %s", (msg_type == RTM_NEWADDR) ? "added" : "removed", argv[3], argv[2]);
        }
    }

    close(fd);
    return (exit_status < 0) ? 1 : 0;
}