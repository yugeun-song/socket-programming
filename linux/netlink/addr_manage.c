/*
 * addr_manage add/del send RTM_NEWADDR and RTM_DELADDR: an IPv4 address changes on
 * a live interface at once, and it needs CAP_NET_ADMIN.  Use a throwaway VM, which
 * also supplies the privilege:
 *
 *     vng --run <bzImage> -- ./linux/bin/netlink/addr_manage add lo 127.9.9.9/32
 *
 * list only reads; of the rest under netlink/, only nflog_listen writes, and only a
 * group binding the kernel drops when the socket closes.
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_addr.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

static void print_usage_helper(const char *const program_name)
{
    fprintf(stderr, "usage: %s list\n", program_name);
    fprintf(stderr, "       %s add <ifname> <addr>/<prefix>\n", program_name);
    fprintf(stderr, "       %s del <ifname> <addr>/<prefix>\n", program_name);
}

static int parse_cidr_helper(struct in_addr *const mut_ip, unsigned char *const mut_prefix,
                             const char *const cidr_text)
{
    const char *slash_at = strchr(cidr_text, '/');
    if (slash_at == NULL) {
        errno = EINVAL;
        return -1;
    }

    char host[INET_ADDRSTRLEN] = { 0 };
    size_t len = (size_t)(slash_at - cidr_text);

    if (len == 0 || len >= sizeof(host)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(host, cidr_text, len);

    if (inet_pton(AF_INET, host, mut_ip) != 1) {
        errno = EINVAL;
        return -1;
    }

    char *endptr = NULL;

    errno = 0;
    unsigned long prefix_bits = strtoul(slash_at + 1, &endptr, 10);
    if (errno != 0 || endptr == slash_at + 1 || *endptr != '\0' || prefix_bits > 32) {
        errno = EINVAL;
        return -1;
    }

    *mut_prefix = (unsigned char)prefix_bits;
    return 0;
}

static void print_addr_helper(const struct nlmsghdr *const nl_header)
{
    const struct ifaddrmsg *addr_info = NLMSG_DATA(nl_header);
    struct rtattr *attr_table[IFA_MAX + 1];

    nl_parse_attrs_helper(IFA_RTA(addr_info), (int)IFA_PAYLOAD(nl_header), attr_table, IFA_MAX);

    const struct rtattr *addr_attr = attr_table[IFA_LOCAL];
    if (addr_attr == NULL) {
        addr_attr = attr_table[IFA_ADDRESS];
    }
    if (addr_attr == NULL) {
        return;
    }

    char addr_text[INET6_ADDRSTRLEN] = { 0 };
    char name[IF_NAMESIZE] = { 0 };

    if (inet_ntop(addr_info->ifa_family, RTA_DATA(addr_attr), addr_text,
                  sizeof(addr_text)) == NULL) {
        return;
    }
    if (if_indextoname(addr_info->ifa_index, name) == NULL) {
        snprintf(name, sizeof(name), "%u", addr_info->ifa_index);
    }

    LOG_RESULT_HELPER("%-10s %s/%u", name, addr_text, addr_info->ifa_prefixlen);
}

static int list_addrs_helper(int fd)
{
    unsigned int seq = nl_next_seq_helper();
    struct addr_request request = { 0 };

    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(request.addr_info));
    request.nl_header.nlmsg_type = RTM_GETADDR;
    request.nl_header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.nl_header.nlmsg_seq = seq;
    request.addr_info.ifa_family = AF_UNSPEC;

    if (nl_send_helper(fd, &request, request.nl_header.nlmsg_len) < 0) {
        return -1;
    }

    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    int is_done = 0;

    while (is_done == 0) {
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
            if (nl_header->nlmsg_seq != seq) {
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
        }
    }

    return 0;
}

static int change_addr_helper(int fd, unsigned short msg_type, unsigned short extra_flags,
                              unsigned int ifindex, const struct in_addr *const ip,
                              unsigned char prefix)
{
    union addr_change request;

    memset(&request, 0, sizeof(request));

    unsigned int seq = nl_next_seq_helper();
    struct ifaddrmsg *addr_info = NLMSG_DATA(&request.nl_header);

    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(*addr_info));
    request.nl_header.nlmsg_type = msg_type;
    request.nl_header.nlmsg_flags = (unsigned short)(NLM_F_REQUEST | NLM_F_ACK | extra_flags);
    request.nl_header.nlmsg_seq = seq;

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

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage_helper(argv[0]);
        return EXIT_FAILURE;
    }

    int is_listing = 0;
    unsigned short msg_type = 0;
    unsigned short extra_flags = 0;

    if (strcmp(argv[1], "list") == 0) {
        is_listing = 1;
    } else if (strcmp(argv[1], "add") == 0) {
        msg_type = RTM_NEWADDR;
        extra_flags = NLM_F_CREATE | NLM_F_EXCL;
    } else if (strcmp(argv[1], "del") == 0) {
        msg_type = RTM_DELADDR;
    } else {
        print_usage_helper(argv[0]);
        return EXIT_FAILURE;
    }

    unsigned int ifindex = 0;
    struct in_addr ip = { 0 };
    unsigned char prefix = 0;

    if (is_listing == 0) {
        if (argc < 4) {
            print_usage_helper(argv[0]);
            return EXIT_FAILURE;
        }
        ifindex = if_nametoindex(argv[2]);
        if (ifindex == 0) {
            LOG_ERRNO_HELPER("if_nametoindex()");
            return EXIT_FAILURE;
        }
        if (parse_cidr_helper(&ip, &prefix, argv[3]) < 0) {
            LOG_MSG_HELPER("bad address '%s', expected <addr>/<prefix>", argv[3]);
            return EXIT_FAILURE;
        }
    }

    int fd = nl_open_helper(NETLINK_ROUTE);
    if (fd < 0) {
        LOG_ERRNO_HELPER("nl_open_helper()");
        return EXIT_FAILURE;
    }

    int result;

    if (is_listing != 0) {
        result = list_addrs_helper(fd);
        if (result < 0) {
            LOG_ERRNO_HELPER("RTM_GETADDR");
        }
    } else {
        result = change_addr_helper(fd, msg_type, extra_flags, ifindex, &ip, prefix);
        if (result < 0) {
            LOG_ERRNO_HELPER((msg_type == RTM_NEWADDR) ? "RTM_NEWADDR" : "RTM_DELADDR");
        } else {
            LOG_RESULT_HELPER("%s %s on %s", (msg_type == RTM_NEWADDR) ? "added" : "removed",
                              argv[3], argv[2]);
        }
    }

    close(fd);
    return (result < 0) ? EXIT_FAILURE : 0;
}
