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

#define RCVBUF_BYTES_HELPER (1024 * 1024)

struct event_counts {
    unsigned long long links;
    unsigned long long addrs;
    unsigned long long routes;
    unsigned long long overruns;
};

static volatile sig_atomic_t g_report_signal_number;

static void on_report_signal_helper(int signal_number)
{
    g_report_signal_number = signal_number;
}

static void print_link_event_helper(struct nlmsghdr *nl_header)
{
    struct rtattr *attr_table[IFLA_MAX + 1];
    struct ifinfomsg *link_info;
    const char *link_name = "?";

    link_info = (struct ifinfomsg *)NLMSG_DATA(nl_header);
    nl_parse_attrs_helper(IFLA_RTA(link_info), (int)IFLA_PAYLOAD(nl_header), attr_table, IFLA_MAX);

    if (attr_table[IFLA_IFNAME] != NULL) {
        link_name = (const char *)RTA_DATA(attr_table[IFLA_IFNAME]);
    }

    LOG_RESULT_HELPER("%s %-10s %s", (nl_header->nlmsg_type == RTM_NEWLINK) ? "link+" : "link-", link_name,
                      (link_info->ifi_flags & IFF_UP) ? "UP" : "DOWN");
}

static void print_addr_event_helper(struct nlmsghdr *nl_header)
{
    char addr_text[INET6_ADDRSTRLEN] = { 0 };
    char link_name[IF_NAMESIZE] = { 0 };
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
    if (if_indextoname(addr_info->ifa_index, link_name) == NULL) {
        snprintf(link_name, sizeof(link_name), "%u", addr_info->ifa_index);
    }

    LOG_RESULT_HELPER("%s %s/%u dev %s", (nl_header->nlmsg_type == RTM_NEWADDR) ? "addr+" : "addr-", addr_text,
                      addr_info->ifa_prefixlen, link_name);
}

static void print_route_event_helper(struct nlmsghdr *nl_header)
{
    char addr_text[INET6_ADDRSTRLEN] = { 0 };
    struct rtattr *attr_table[RTA_MAX + 1];
    struct rtmsg *route_info;

    route_info = (struct rtmsg *)NLMSG_DATA(nl_header);
    nl_parse_attrs_helper(RTM_RTA(route_info), (int)RTM_PAYLOAD(nl_header), attr_table, RTA_MAX);

    if (attr_table[RTA_DST] == NULL) {
        snprintf(addr_text, sizeof(addr_text), "default");
    } else if (inet_ntop(route_info->rtm_family, RTA_DATA(attr_table[RTA_DST]), addr_text, sizeof(addr_text)) == NULL) {
        return;
    }

    LOG_RESULT_HELPER("%s %s/%u table %u", (nl_header->nlmsg_type == RTM_NEWROUTE) ? "route+" : "route-", addr_text,
                      route_info->rtm_dst_len, route_info->rtm_table);
}

int main(void)
{
    static const unsigned int event_groups[] = { RTNLGRP_LINK, RTNLGRP_IPV4_IFADDR, RTNLGRP_IPV6_IFADDR,
                                                 RTNLGRP_IPV4_ROUTE };
    struct event_counts counts = { 0 };
    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    struct nlmsghdr *nl_header;
    sigset_t wait_signal_mask;
    sigset_t signals_to_block;
    size_t i;
    ssize_t received_bytes;
    int report_signal_number;
    int mask_error_code;
    int len;
    int fd;

    if (catch_shutdown_signals_helper(&wait_signal_mask) < 0) {
        LOG_ERRNO_HELPER("catch_shutdown_signals_helper()");
        return 1;
    }
    if (register_signal_handler_helper(SIGUSR1, on_report_signal_helper, SA_RESTART) < 0) {
        LOG_ERRNO_HELPER("sigaction(SIGUSR1)");
        return 1;
    }
    if (sigemptyset(&signals_to_block) < 0) {
        LOG_ERRNO_HELPER("sigemptyset()");
        return 1;
    }
    if (sigaddset(&signals_to_block, SIGUSR1) < 0) {
        LOG_ERRNO_HELPER("sigaddset()");
        return 1;
    }

    mask_error_code = pthread_sigmask(SIG_BLOCK, &signals_to_block, NULL);
    if (mask_error_code != 0) {
        errno = mask_error_code;
        LOG_ERRNO_HELPER("pthread_sigmask()");
        return 1;
    }

    fd = nl_open_helper(NETLINK_ROUTE);
    if (fd < 0) {
        LOG_ERRNO_HELPER("nl_open_helper()");
        return 1;
    }

    if (nl_set_rcvbuf_helper(fd, RCVBUF_BYTES_HELPER) < 0) {
        LOG_ERRNO_HELPER("setsockopt(SO_RCVBUF)");
        close(fd);
        return 1;
    }

    for (i = 0; i < sizeof(event_groups) / sizeof(event_groups[0]); ++i) {
        if (nl_join_group_helper(fd, event_groups[i]) < 0) {
            LOG_ERRNO_HELPER("setsockopt(NETLINK_ADD_MEMBERSHIP)");
            close(fd);
            return 1;
        }
    }

    LOG_MSG_HELPER("watching link, address and IPv4 route events");

    while (!should_shut_down_helper()) {
        report_signal_number = (int)g_report_signal_number;
        if (report_signal_number != 0) {
            g_report_signal_number = 0;
            LOG_MSG_HELPER("signal %d: %llu link, %llu address, %llu route events, %llu overruns so far",
                           report_signal_number, counts.links, counts.addrs, counts.routes, counts.overruns);
        }

        received_bytes = nl_recv_or_shutdown_helper(fd, nl_recv_buf, sizeof(nl_recv_buf), &wait_signal_mask);
        if (received_bytes < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ENOBUFS) {
                counts.overruns += 1;
                LOG_MSG_HELPER("the kernel dropped events, this socket overflowed");
                continue;
            }
            LOG_ERRNO_HELPER("nl_recv_helper()");
            close(fd);
            return 1;
        }

        len = (int)received_bytes;
        nl_header = (struct nlmsghdr *)nl_recv_buf;

        while (NLMSG_OK(nl_header, len)) {
            switch (nl_header->nlmsg_type) {
            case RTM_NEWLINK:
            case RTM_DELLINK:
                print_link_event_helper(nl_header);
                counts.links += 1;
                break;
            case RTM_NEWADDR:
            case RTM_DELADDR:
                print_addr_event_helper(nl_header);
                counts.addrs += 1;
                break;
            case RTM_NEWROUTE:
            case RTM_DELROUTE:
                print_route_event_helper(nl_header);
                counts.routes += 1;
                break;
            default:
                break;
            }
            nl_header = NLMSG_NEXT(nl_header, len);
        }
    }

    LOG_MSG_HELPER("stopped by signal %d", which_shutdown_signal_helper());

    close(fd);
    return 0;
}