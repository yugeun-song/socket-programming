#define _DEFAULT_SOURCE

#include <errno.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/log.h"
#include "netlink/nl_util.h"

union link_request {
    struct nlmsghdr nl_header;
    char raw[NLMSG_SPACE(sizeof(struct ifinfomsg)) + RTA_SPACE(sizeof(unsigned int))];
};

static const char *get_link_type_name_helper(unsigned short arp_type)
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

static void print_link_helper(const struct nlmsghdr *const nl_header)
{
    const struct ifinfomsg *link_info = NLMSG_DATA(nl_header);
    struct rtattr *attr_table[IFLA_MAX + 1];

    nl_parse_attrs_helper(attr_table, IFLA_MAX, IFLA_RTA(link_info), (int)IFLA_PAYLOAD(nl_header));

    const char *link_name = "?";

    if (attr_table[IFLA_IFNAME] != NULL) {
        link_name = RTA_DATA(attr_table[IFLA_IFNAME]);
    }

    unsigned int mtu = 0;

    if (attr_table[IFLA_MTU] != NULL) {
        memcpy(&mtu, RTA_DATA(attr_table[IFLA_MTU]), sizeof(mtu));
    }

    char mac_field[32] = { 0 };

    if (attr_table[IFLA_ADDRESS] != NULL && RTA_PAYLOAD(attr_table[IFLA_ADDRESS]) == ETH_ALEN) {
        const unsigned char *mac_bytes = RTA_DATA(attr_table[IFLA_ADDRESS]);

        snprintf(mac_field, sizeof(mac_field), " mac %02x:%02x:%02x:%02x:%02x:%02x", mac_bytes[0],
                 mac_bytes[1], mac_bytes[2], mac_bytes[3], mac_bytes[4], mac_bytes[5]);
    }

    char output_line[256];

    snprintf(output_line, sizeof(output_line), "%2d %-10s %-9s mtu %-6u %s%s%s",
             link_info->ifi_index, link_name, get_link_type_name_helper(link_info->ifi_type), mtu,
             ((link_info->ifi_flags & IFF_UP) != 0) ? "UP" : "DOWN",
             ((link_info->ifi_flags & IFF_RUNNING) != 0) ? ",RUNNING" : "", mac_field);
    LOG_RESULT_HELPER("%s", output_line);
}

int main(void)
{
    int fd = nl_open_helper(NETLINK_ROUTE);
    if (fd < 0) {
        LOG_ERRNO_HELPER("nl_open_helper()");
        return EXIT_FAILURE;
    }

    int exit_status = EXIT_FAILURE;
    unsigned int seq = nl_get_next_seq_helper();
    union link_request request;

    memset(&request, 0, sizeof(request));

    struct ifinfomsg *link_info = NLMSG_DATA(&request.nl_header);

    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(*link_info));
    request.nl_header.nlmsg_type = RTM_GETLINK;
    request.nl_header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.nl_header.nlmsg_seq = seq;
    link_info->ifi_family = AF_UNSPEC;

    unsigned int ext_mask = RTEXT_FILTER_VF;

    if (nl_add_attr_helper(&request.nl_header, sizeof(request), IFLA_EXT_MASK, &ext_mask,
                           sizeof(ext_mask)) < 0) {
        LOG_ERRNO_HELPER("nl_add_attr_helper(IFLA_EXT_MASK)");
        goto out_close_fd;
    }
    if (nl_send_helper(fd, &request, request.nl_header.nlmsg_len) < 0) {
        LOG_ERRNO_HELPER("nl_send_helper()");
        goto out_close_fd;
    }

    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    unsigned int links = 0;
    int is_done = 0;

    while (is_done == 0) {
        ssize_t received_bytes = nl_recv_or_shutdown_helper(fd, nl_recv_buf,
                                                            sizeof(nl_recv_buf), NULL);
        if (received_bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERRNO_HELPER("nl_recv_helper()");
            goto out_close_fd;
        }

        ssize_t remaining = received_bytes;

        for (const struct nlmsghdr *nl_header = (const struct nlmsghdr *)nl_recv_buf;
             NLMSG_OK(nl_header, remaining); nl_header = NLMSG_NEXT(nl_header, remaining)) {
            if (nl_header->nlmsg_seq != seq) {
                continue;
            }
            if ((nl_header->nlmsg_flags & NLM_F_DUMP_INTR) != 0) {
                LOG_MSG_HELPER("dump raced with a link change, result is incomplete");
                goto out_close_fd;
            }
            if (nl_header->nlmsg_type == NLMSG_DONE) {
                if (nl_check_done_helper(nl_header) < 0) {
                    LOG_ERRNO_HELPER("RTM_GETLINK");
                    goto out_close_fd;
                }
                is_done = 1;
                break;
            }
            if (nl_check_error_helper(nl_header) < 0) {
                LOG_ERRNO_HELPER("RTM_GETLINK");
                goto out_close_fd;
            }
            if (nl_header->nlmsg_type == RTM_NEWLINK) {
                print_link_helper(nl_header);
                ++links;
            }
        }
    }

    LOG_RESULT_HELPER("%u links", links);
    exit_status = 0;

out_close_fd:
    close(fd);
    return exit_status;
}
