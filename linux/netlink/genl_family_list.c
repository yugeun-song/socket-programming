#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include <linux/genetlink.h>
#include <linux/netlink.h>

#include "common/log.h"
#include "netlink/nl_util.h"

struct genl_request {
    struct nlmsghdr nl_header;
    struct genlmsghdr genl;
};

static unsigned int count_nested_helper(struct rtattr *nest)
{
    struct rtattr *attr;
    unsigned int count = 0;
    int remaining;

    attr = (struct rtattr *)RTA_DATA(nest);
    remaining = (int)RTA_PAYLOAD(nest);

    while (RTA_OK(attr, remaining)) {
        ++count;
        attr = RTA_NEXT(attr, remaining);
    }

    return count;
}

static void print_family_helper(struct nlmsghdr *nl_header)
{
    struct rtattr *attr_table[CTRL_ATTR_MAX + 1];
    const char *name = "?";
    unsigned int version = 0;
    unsigned int groups = 0;
    unsigned short id = 0;

    if (nl_header->nlmsg_len < NLMSG_LENGTH(GENL_HDRLEN)) {
        return;
    }

    nl_parse_attrs_helper((struct rtattr *)((char *)NLMSG_DATA(nl_header) + GENL_HDRLEN),
                          (int)NLMSG_PAYLOAD(nl_header, GENL_HDRLEN), attr_table, CTRL_ATTR_MAX);

    if (attr_table[CTRL_ATTR_FAMILY_NAME] != NULL) {
        name = (const char *)RTA_DATA(attr_table[CTRL_ATTR_FAMILY_NAME]);
    }
    if (attr_table[CTRL_ATTR_FAMILY_ID] != NULL) {
        memcpy(&id, RTA_DATA(attr_table[CTRL_ATTR_FAMILY_ID]), sizeof(id));
    }
    if (attr_table[CTRL_ATTR_VERSION] != NULL) {
        memcpy(&version, RTA_DATA(attr_table[CTRL_ATTR_VERSION]), sizeof(version));
    }
    if (attr_table[CTRL_ATTR_MCAST_GROUPS] != NULL) {
        groups = count_nested_helper(attr_table[CTRL_ATTR_MCAST_GROUPS]);
    }

    LOG_RESULT_HELPER("id %-4u v%-2u %-20s %u multicast groups", id, version, name, groups);
}

int main(void)
{
    struct genl_request request = { 0 };
    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    struct nlmsghdr *nl_header;
    unsigned int seq;
    unsigned int families = 0;
    ssize_t received_bytes;
    int is_done = 0;
    int remaining;
    int fd;

    fd = nl_open_helper(NETLINK_GENERIC);
    if (fd < 0) {
        LOG_ERRNO_HELPER("nl_open_helper(NETLINK_GENERIC)");
        return 1;
    }

    seq = nl_next_seq_helper();
    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(request.genl));
    request.nl_header.nlmsg_type = GENL_ID_CTRL;
    request.nl_header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.nl_header.nlmsg_seq = seq;
    request.genl.cmd = CTRL_CMD_GETFAMILY;
    request.genl.version = 1;

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
                LOG_MSG_HELPER("dump raced with a family change, result is incomplete");
                close(fd);
                return 1;
            }
            if (nl_header->nlmsg_type == NLMSG_DONE) {
                if (nl_check_done_helper(nl_header) < 0) {
                    LOG_ERRNO_HELPER("CTRL_CMD_GETFAMILY");
                    close(fd);
                    return 1;
                }
                is_done = 1;
                break;
            }
            if (nl_check_error_helper(nl_header) < 0) {
                LOG_ERRNO_HELPER("CTRL_CMD_GETFAMILY");
                close(fd);
                return 1;
            }
            if (nl_header->nlmsg_type == GENL_ID_CTRL) {
                print_family_helper(nl_header);
                ++families;
            }
            nl_header = NLMSG_NEXT(nl_header, remaining);
        }
    }

    LOG_RESULT_HELPER("%u generic netlink families", families);

    close(fd);
    return 0;
}