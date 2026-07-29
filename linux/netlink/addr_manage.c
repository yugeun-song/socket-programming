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

#define DUMP_TIMEOUT_MS 5000
#define ACK_TIMEOUT_MS 5000

struct addr_request {
    struct nlmsghdr nlh;
    struct ifaddrmsg ifa;
};

union addr_change {
    struct nlmsghdr nlh;
    char raw[NLMSG_SPACE(sizeof(struct ifaddrmsg)) + 2 * RTA_SPACE(sizeof(struct in_addr))];
};

static void usage(const char *argv0)
{
    fprintf(stderr, "usage: %s list\n", argv0);
    fprintf(stderr, "       %s add <ifname> <addr>/<prefix>\n", argv0);
    fprintf(stderr, "       %s del <ifname> <addr>/<prefix>\n", argv0);
}

static int parse_cidr(const char *text, struct in_addr *ip, unsigned char *prefix)
{
    char host[INET_ADDRSTRLEN] = { 0 };
    const char *slash;
    unsigned long bits;
    char *endptr;
    size_t len;

    slash = strchr(text, '/');
    if (slash == NULL) {
        errno = EINVAL;
        return -1;
    }

    len = (size_t)(slash - text);
    if (len == 0 || len >= sizeof(host)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(host, text, len);

    if (inet_pton(AF_INET, host, ip) != 1) {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    bits = strtoul(slash + 1, &endptr, 10);
    if (errno != 0 || endptr == slash + 1 || *endptr != '\0' || bits > 32) {
        errno = EINVAL;
        return -1;
    }

    *prefix = (unsigned char)bits;
    return 0;
}

static void print_addr(struct nlmsghdr *nlh)
{
    char text[INET6_ADDRSTRLEN] = { 0 };
    char name[IF_NAMESIZE] = { 0 };
    struct rtattr *tb[IFA_MAX + 1];
    struct ifaddrmsg *ifa;
    struct rtattr *src;

    ifa = (struct ifaddrmsg *)NLMSG_DATA(nlh);
    nl_parse_attrs(IFA_RTA(ifa), (int)IFA_PAYLOAD(nlh), tb, IFA_MAX);

    src = (tb[IFA_LOCAL] != NULL) ? tb[IFA_LOCAL] : tb[IFA_ADDRESS];
    if (src == NULL) {
        return;
    }
    if (inet_ntop(ifa->ifa_family, RTA_DATA(src), text, sizeof(text)) == NULL) {
        return;
    }
    if (if_indextoname(ifa->ifa_index, name) == NULL) {
        snprintf(name, sizeof(name), "%u", ifa->ifa_index);
    }

    printf("addr_manage: %-10s %s/%u\n", name, text, ifa->ifa_prefixlen);
}

static int list_addrs(int fd)
{
    struct addr_request req = { 0 };
    char buf[NL_BUF_SIZE];
    struct nlmsghdr *nlh;
    struct deadline dl;
    unsigned int seq;
    ssize_t n;
    int is_done = 0;
    int len;

    seq = nl_next_seq();
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ifa));
    req.nlh.nlmsg_type = RTM_GETADDR;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = seq;
    req.ifa.ifa_family = AF_UNSPEC;

    if (nl_send(fd, &req, req.nlh.nlmsg_len) < 0) {
        return -1;
    }

    if (deadline_start(&dl, DUMP_TIMEOUT_MS) < 0) {
        return -1;
    }

    while (!is_done) {
        n = nl_recv_until(fd, buf, sizeof(buf), &dl, NULL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        len = (int)n;
        nlh = (struct nlmsghdr *)buf;

        while (NLMSG_OK(nlh, len)) {
            if (nlh->nlmsg_seq != seq) {
                nlh = NLMSG_NEXT(nlh, len);
                continue;
            }
            if (nlh->nlmsg_type == NLMSG_DONE) {
                if (nl_check_done(nlh) < 0) {
                    return -1;
                }
                is_done = 1;
                break;
            }
            if (nl_check_error(nlh) < 0) {
                return -1;
            }
            if (nlh->nlmsg_type == RTM_NEWADDR) {
                print_addr(nlh);
            }
            nlh = NLMSG_NEXT(nlh, len);
        }
    }

    return 0;
}

static int change_addr(int fd, unsigned short type, unsigned short extra_flags, unsigned int ifindex,
                       const struct in_addr *ip, unsigned char prefix)
{
    union addr_change req;
    char buf[NL_BUF_SIZE];
    struct ifaddrmsg *ifa;
    struct nlmsghdr *nlh;
    struct deadline dl;
    unsigned int seq;
    ssize_t n;
    int len;

    memset(&req, 0, sizeof(req));

    seq = nl_next_seq();
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(*ifa));
    req.nlh.nlmsg_type = type;
    req.nlh.nlmsg_flags = (unsigned short)(NLM_F_REQUEST | NLM_F_ACK | extra_flags);
    req.nlh.nlmsg_seq = seq;

    ifa = (struct ifaddrmsg *)NLMSG_DATA(&req.nlh);
    ifa->ifa_family = AF_INET;
    ifa->ifa_prefixlen = prefix;
    ifa->ifa_index = ifindex;
    ifa->ifa_scope = RT_SCOPE_UNIVERSE;

    if (nl_add_attr(&req.nlh, sizeof(req), IFA_LOCAL, ip, sizeof(*ip)) < 0) {
        return -1;
    }
    if (nl_add_attr(&req.nlh, sizeof(req), IFA_ADDRESS, ip, sizeof(*ip)) < 0) {
        return -1;
    }

    if (nl_send(fd, &req, req.nlh.nlmsg_len) < 0) {
        return -1;
    }

    if (deadline_start(&dl, ACK_TIMEOUT_MS) < 0) {
        return -1;
    }

    while (1) {
        n = nl_recv_until(fd, buf, sizeof(buf), &dl, NULL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        len = (int)n;
        nlh = (struct nlmsghdr *)buf;

        while (NLMSG_OK(nlh, len)) {
            if (nlh->nlmsg_seq == seq && nlh->nlmsg_type == NLMSG_ERROR) {
                return nl_check_error(nlh);
            }
            nlh = NLMSG_NEXT(nlh, len);
        }
    }
}

int main(int argc, char **argv)
{
    unsigned short extra_flags = 0;
    unsigned short type = 0;
    unsigned char prefix = 0;
    unsigned int ifindex = 0;
    struct in_addr ip = { 0 };
    int is_listing = 0;
    int rc;
    int fd;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "list") == 0) {
        is_listing = 1;
    } else if (strcmp(argv[1], "add") == 0) {
        type = RTM_NEWADDR;
        extra_flags = NLM_F_CREATE | NLM_F_EXCL;
    } else if (strcmp(argv[1], "del") == 0) {
        type = RTM_DELADDR;
    } else {
        usage(argv[0]);
        return 1;
    }

    if (!is_listing) {
        if (argc < 4) {
            usage(argv[0]);
            return 1;
        }
        ifindex = if_nametoindex(argv[2]);
        if (ifindex == 0) {
            log_errno("if_nametoindex()");
            return 1;
        }
        if (parse_cidr(argv[3], &ip, &prefix) < 0) {
            log_msg("bad address '%s', expected <addr>/<prefix>", argv[3]);
            return 1;
        }
    }

    fd = nl_open(NETLINK_ROUTE);
    if (fd < 0) {
        log_errno("nl_open()");
        return 1;
    }

    if (is_listing) {
        rc = list_addrs(fd);
        if (rc < 0) {
            log_errno("RTM_GETADDR");
        }
    } else {
        rc = change_addr(fd, type, extra_flags, ifindex, &ip, prefix);
        if (rc < 0) {
            log_errno((type == RTM_NEWADDR) ? "RTM_NEWADDR" : "RTM_DELADDR");
        } else {
            printf("addr_manage: %s %s on %s\n", (type == RTM_NEWADDR) ? "added" : "removed", argv[3], argv[2]);
        }
    }

    close(fd);
    return (rc < 0) ? 1 : 0;
}