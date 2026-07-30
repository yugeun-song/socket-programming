#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#include <linux/if_addr.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "common/log.h"
#include "common/net_util.h"
#include "netlink/nl_util.h"

#define RCVBUF_BYTES (1024 * 1024)

struct event_counts {
    unsigned long long links;
    unsigned long long addrs;
    unsigned long long routes;
    unsigned long long overruns;
};

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_report;

static void on_stop(int signo __attribute__((unused)))
{
    g_stop = 1;
}

static void on_report(int signo __attribute__((unused)))
{
    g_report = 1;
}

static void print_link_event(struct nlmsghdr *nlh)
{
    struct rtattr *tb[IFLA_MAX + 1];
    struct ifinfomsg *ifi;
    const char *name = "?";

    ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);
    nl_parse_attrs(IFLA_RTA(ifi), (int)IFLA_PAYLOAD(nlh), tb, IFLA_MAX);

    if (tb[IFLA_IFNAME] != NULL) {
        name = (const char *)RTA_DATA(tb[IFLA_IFNAME]);
    }

    printf("route_monitor: %s %-10s %s\n", (nlh->nlmsg_type == RTM_NEWLINK) ? "link+" : "link-", name,
           (ifi->ifi_flags & IFF_UP) ? "UP" : "DOWN");
}

static void print_addr_event(struct nlmsghdr *nlh)
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

    printf("route_monitor: %s %s/%u dev %s\n", (nlh->nlmsg_type == RTM_NEWADDR) ? "addr+" : "addr-", text, ifa->ifa_prefixlen, name);
}

static void print_route_event(struct nlmsghdr *nlh)
{
    char text[INET6_ADDRSTRLEN] = { 0 };
    struct rtattr *tb[RTA_MAX + 1];
    struct rtmsg *rtm;

    rtm = (struct rtmsg *)NLMSG_DATA(nlh);
    nl_parse_attrs(RTM_RTA(rtm), (int)RTM_PAYLOAD(nlh), tb, RTA_MAX);

    if (tb[RTA_DST] == NULL) {
        snprintf(text, sizeof(text), "default");
    } else if (inet_ntop(rtm->rtm_family, RTA_DATA(tb[RTA_DST]), text, sizeof(text)) == NULL) {
        return;
    }

    printf("route_monitor: %s %s/%u table %u\n", (nlh->nlmsg_type == RTM_NEWROUTE) ? "route+" : "route-", text,
           rtm->rtm_dst_len, rtm->rtm_table);
}

int main(void)
{
    static const unsigned int groups[] = { RTNLGRP_LINK, RTNLGRP_IPV4_IFADDR, RTNLGRP_IPV6_IFADDR, RTNLGRP_IPV4_ROUTE };
    static const int watched_signals[] = { SIGINT, SIGTERM, SIGUSR1 };
    struct event_counts counts = { 0 };
    char buf[NL_BUF_SIZE];
    struct nlmsghdr *nlh;
    struct deadline dl;
    sigset_t saved_mask;
    size_t i;
    ssize_t n;
    int len;
    int fd;

    if (install_signal_handler(SIGINT, on_stop, SIGNAL_INTERRUPTS) < 0 ||
        install_signal_handler(SIGTERM, on_stop, SIGNAL_INTERRUPTS) < 0) {
        LOG_ERRNO("sigaction(stop)");
        return 1;
    }
    if (install_signal_handler(SIGUSR1, on_report, SIGNAL_RESTARTS) < 0) {
        LOG_ERRNO("sigaction(SIGUSR1)");
        return 1;
    }
    if (block_signals(watched_signals, sizeof(watched_signals) / sizeof(watched_signals[0]), &saved_mask) < 0) {
        LOG_ERRNO("pthread_sigmask()");
        return 1;
    }

    fd = nl_open(NETLINK_ROUTE);
    if (fd < 0) {
        LOG_ERRNO("nl_open()");
        return 1;
    }

    if (nl_set_rcvbuf(fd, RCVBUF_BYTES) < 0) {
        LOG_ERRNO("setsockopt(SO_RCVBUF)");
        close(fd);
        return 1;
    }

    for (i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i) {
        if (nl_join_group(fd, groups[i]) < 0) {
            LOG_ERRNO("setsockopt(NETLINK_ADD_MEMBERSHIP)");
            close(fd);
            return 1;
        }
    }

    LOG_MSG("watching link, address and IPv4 route events");

    if (deadline_start(&dl, DEADLINE_FOREVER) < 0) {
        LOG_ERRNO("clock_gettime()");
        close(fd);
        return 1;
    }

    while (!g_stop) {
        if (g_report) {
            g_report = 0;
            LOG_MSG("%llu link, %llu address, %llu route events, %llu overruns so far", counts.links, counts.addrs,
                    counts.routes, counts.overruns);
        }

        n = nl_recv_until(fd, buf, sizeof(buf), &dl, &saved_mask);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ENOBUFS) {
                counts.overruns += 1;
                LOG_MSG("the kernel dropped events, this socket overflowed");
                continue;
            }
            LOG_ERRNO("nl_recv()");
            close(fd);
            return 1;
        }

        len = (int)n;
        nlh = (struct nlmsghdr *)buf;

        while (NLMSG_OK(nlh, len)) {
            switch (nlh->nlmsg_type) {
            case RTM_NEWLINK:
            case RTM_DELLINK:
                print_link_event(nlh);
                counts.links += 1;
                break;
            case RTM_NEWADDR:
            case RTM_DELADDR:
                print_addr_event(nlh);
                counts.addrs += 1;
                break;
            case RTM_NEWROUTE:
            case RTM_DELROUTE:
                print_route_event(nlh);
                counts.routes += 1;
                break;
            default:
                break;
            }
            nlh = NLMSG_NEXT(nlh, len);
        }
        fflush(stdout);
    }

    LOG_MSG("stopped");

    close(fd);
    return 0;
}