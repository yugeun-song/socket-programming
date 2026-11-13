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

union link_request {
    struct nlmsghdr nl_header;
    char raw[NLMSG_SPACE(sizeof(struct ifinfomsg)) + RTA_SPACE(sizeof(unsigned int))];
};

static const char *link_type_helper(unsigned short arp_type)
{
    switch (arp_type) {
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

static void print_link_helper(struct nlmsghdr *nl_header)
{
    struct rtattr *attr_table[IFLA_MAX + 1];
    char mac_field[32] = { 0 };
    const unsigned char *mac_bytes;
    struct ifinfomsg *link_info;
    const char *link_name = "?";
    unsigned int mtu = 0;
    char output_line[256];

    link_info = (struct ifinfomsg *)NLMSG_DATA(nl_header);
    nl_parse_attrs_helper(IFLA_RTA(link_info), (int)IFLA_PAYLOAD(nl_header), attr_table, IFLA_MAX);

    if (attr_table[IFLA_IFNAME] != NULL) {
        link_name = (const char *)RTA_DATA(attr_table[IFLA_IFNAME]);
    }
    if (attr_table[IFLA_MTU] != NULL) {
        memcpy(&mtu, RTA_DATA(attr_table[IFLA_MTU]), sizeof(mtu));
    }
    if (attr_table[IFLA_ADDRESS] != NULL && RTA_PAYLOAD(attr_table[IFLA_ADDRESS]) == ETH_ALEN) {
        mac_bytes = (const unsigned char *)RTA_DATA(attr_table[IFLA_ADDRESS]);
        snprintf(mac_field, sizeof(mac_field), " mac %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_bytes[0], mac_bytes[1], mac_bytes[2], mac_bytes[3], mac_bytes[4], mac_bytes[5]);
    }

    snprintf(output_line, sizeof(output_line), "%2d %-10s %-9s mtu %-6u %s%s%s", link_info->ifi_index, link_name,
             link_type_helper(link_info->ifi_type), mtu, (link_info->ifi_flags & IFF_UP) ? "UP" : "DOWN",
             (link_info->ifi_flags & IFF_RUNNING) ? ",RUNNING" : "", mac_field);
    LOG_RESULT_HELPER("%s", output_line);
}

int main(void)
{
    unsigned int ext_mask = RTEXT_FILTER_VF;
    union link_request request;
    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    struct ifinfomsg *link_info;
    struct nlmsghdr *nl_header;
    unsigned int seq;
    unsigned int links = 0;
    ssize_t received_bytes;
    int is_done = 0;
    int remaining;
    int fd;

    fd = nl_open_helper(NETLINK_ROUTE);
    if (fd < 0) {
        LOG_ERRNO_HELPER("nl_open_helper()");
        return 1;
    }

    memset(&request, 0, sizeof(request));

    seq = nl_next_seq_helper();
    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(*link_info));
    request.nl_header.nlmsg_type = RTM_GETLINK;
    request.nl_header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.nl_header.nlmsg_seq = seq;

    link_info = (struct ifinfomsg *)NLMSG_DATA(&request.nl_header);
    link_info->ifi_family = AF_UNSPEC;

    if (nl_add_attr_helper(&request.nl_header, sizeof(request), IFLA_EXT_MASK, &ext_mask, sizeof(ext_mask)) < 0) {
        LOG_ERRNO_HELPER("nl_add_attr_helper(IFLA_EXT_MASK)");
        close(fd);
        return 1;
    }

    if (nl_send_helper(fd, &request, request.nl_header.nlmsg_len) < 0) {
        LOG_ERRNO_HELPER("nl_send_helper()");
        close(fd);
        return 1;
    }

    while (!is_done) {
        received_bytes = nl_recv_or_shutdown_helper(fd, nl_recv_buf, sizeof(nl_recv_buf), NULL);
        if (received_bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERRNO_HELPER("nl_recv_helper()");
            close(fd);
            return 1;
        }

        remaining = (int)received_bytes;
        nl_header = (struct nlmsghdr *)nl_recv_buf;

        while (NLMSG_OK(nl_header, remaining)) {
            if (nl_header->nlmsg_seq != seq) {
                nl_header = NLMSG_NEXT(nl_header, remaining);
                continue;
            }
            if (nl_header->nlmsg_flags & NLM_F_DUMP_INTR) {
                LOG_MSG_HELPER("dump raced with a link change, result is incomplete");
                close(fd);
                return 1;
            }
            if (nl_header->nlmsg_type == NLMSG_DONE) {
                if (nl_check_done_helper(nl_header) < 0) {
                    LOG_ERRNO_HELPER("RTM_GETLINK");
                    close(fd);
                    return 1;
                }
                is_done = 1;
                break;
            }
            if (nl_check_error_helper(nl_header) < 0) {
                LOG_ERRNO_HELPER("RTM_GETLINK");
                close(fd);
                return 1;
            }
            if (nl_header->nlmsg_type == RTM_NEWLINK) {
                print_link_helper(nl_header);
                ++links;
            }
            nl_header = NLMSG_NEXT(nl_header, remaining);
        }
    }

    LOG_RESULT_HELPER("%u links", links);

    close(fd);
    return 0;
}