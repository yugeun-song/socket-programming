#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>

#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "common/log.h"
#include "netlink/nl_util.h"

#define DUMP_TIMEOUT_MS 5000

union link_request {
    struct nlmsghdr nlh;
    char raw[NLMSG_SPACE(sizeof(struct ifinfomsg)) + RTA_SPACE(sizeof(unsigned int))];
};

static const char *link_type(unsigned short type)
{
    switch (type) {
    case ARPHRD_ETHER:
        return "ether";
    case ARPHRD_LOOPBACK:
        return "loopback";
    case ARPHRD_IEEE80211:
        return "ieee80211";
    case ARPHRD_PPP:
        return "ppp";
    case ARPHRD_SIT:
        return "sit";
    case ARPHRD_TUNNEL:
        return "tunnel";
    case ARPHRD_NONE:
        return "none";
    default:
        return "other";
    }
}

static void print_link(struct nlmsghdr *nlh)
{
    struct rtattr *tb[IFLA_MAX + 1];
    char hwaddr[32] = { 0 };
    const unsigned char *mac;
    struct ifinfomsg *ifi;
    const char *name = "?";
    unsigned int mtu = 0;
    char line[256];

    ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);
    nl_parse_attrs(IFLA_RTA(ifi), (int)IFLA_PAYLOAD(nlh), tb, IFLA_MAX);

    if (tb[IFLA_IFNAME] != NULL) {
        name = (const char *)RTA_DATA(tb[IFLA_IFNAME]);
    }
    if (tb[IFLA_MTU] != NULL) {
        memcpy(&mtu, RTA_DATA(tb[IFLA_MTU]), sizeof(mtu));
    }
    if (tb[IFLA_ADDRESS] != NULL && RTA_PAYLOAD(tb[IFLA_ADDRESS]) == ETH_ALEN) {
        mac = (const unsigned char *)RTA_DATA(tb[IFLA_ADDRESS]);
        snprintf(hwaddr, sizeof(hwaddr), " mac %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    snprintf(line, sizeof(line), "link_dump: %2d %-10s %-9s mtu %-6u %s%s%s", ifi->ifi_index, name, link_type(ifi->ifi_type),
             mtu, (ifi->ifi_flags & IFF_UP) ? "UP" : "DOWN", (ifi->ifi_flags & IFF_RUNNING) ? ",RUNNING" : "", hwaddr);
    printf("%s\n", line);
}

int main(void)
{
    unsigned int ext_mask = RTEXT_FILTER_VF;
    union link_request req;
    char buf[NL_BUF_SIZE];
    struct ifinfomsg *ifi;
    struct nlmsghdr *nlh;
    struct deadline dl;
    unsigned int seq;
    unsigned int links = 0;
    ssize_t n;
    int is_done = 0;
    int len;
    int fd;

    fd = nl_open(NETLINK_ROUTE);
    if (fd < 0) {
        log_errno("nl_open()");
        return 1;
    }

    memset(&req, 0, sizeof(req));

    seq = nl_next_seq();
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(*ifi));
    req.nlh.nlmsg_type = RTM_GETLINK;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = seq;

    ifi = (struct ifinfomsg *)NLMSG_DATA(&req.nlh);
    ifi->ifi_family = AF_UNSPEC;

    if (nl_add_attr(&req.nlh, sizeof(req), IFLA_EXT_MASK, &ext_mask, sizeof(ext_mask)) < 0) {
        log_errno("nl_add_attr(IFLA_EXT_MASK)");
        close(fd);
        return 1;
    }

    if (nl_send(fd, &req, req.nlh.nlmsg_len) < 0) {
        log_errno("nl_send()");
        close(fd);
        return 1;
    }

    if (deadline_start(&dl, DUMP_TIMEOUT_MS) < 0) {
        log_errno("clock_gettime()");
        close(fd);
        return 1;
    }

    while (!is_done) {
        n = nl_recv_until(fd, buf, sizeof(buf), &dl, NULL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_errno("nl_recv()");
            close(fd);
            return 1;
        }

        len = (int)n;
        nlh = (struct nlmsghdr *)buf;

        while (NLMSG_OK(nlh, len)) {
            if (nlh->nlmsg_seq != seq) {
                nlh = NLMSG_NEXT(nlh, len);
                continue;
            }
            if (nlh->nlmsg_flags & NLM_F_DUMP_INTR) {
                log_msg("dump raced with a link change, result is incomplete");
                close(fd);
                return 1;
            }
            if (nlh->nlmsg_type == NLMSG_DONE) {
                if (nl_check_done(nlh) < 0) {
                    log_errno("RTM_GETLINK");
                    close(fd);
                    return 1;
                }
                is_done = 1;
                break;
            }
            if (nl_check_error(nlh) < 0) {
                log_errno("RTM_GETLINK");
                close(fd);
                return 1;
            }
            if (nlh->nlmsg_type == RTM_NEWLINK) {
                print_link(nlh);
                ++links;
            }
            nlh = NLMSG_NEXT(nlh, len);
        }
    }

    printf("link_dump: %u links\n", links);

    close(fd);
    return 0;
}