#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/log.h"
#include "netlink/nl_util.h"

struct genl_request {
    struct nlmsghdr nl_header;
    struct genlmsghdr genl;
};

static unsigned int count_nested_helper(const struct rtattr *const nest)
{
    unsigned int count = 0;
    int remaining = (int)RTA_PAYLOAD(nest);

    for (const struct rtattr *attr = RTA_DATA(nest); RTA_OK(attr, remaining);
         attr = RTA_NEXT(attr, remaining)) {
        ++count;
    }

    return count;
}

static void print_family_helper(const struct nlmsghdr *const nl_header)
{
    if (nl_header->nlmsg_len < NLMSG_LENGTH(GENL_HDRLEN)) {
        return;
    }

    const char *message_data = NLMSG_DATA(nl_header);
    struct rtattr *attr_table[CTRL_ATTR_MAX + 1];

    nl_parse_attrs_helper(attr_table, CTRL_ATTR_MAX,
                          (const struct rtattr *)(message_data + GENL_HDRLEN),
                          (int)NLMSG_PAYLOAD(nl_header, GENL_HDRLEN));

    const char *name = "?";
    unsigned short id = 0;
    unsigned int version = 0;
    unsigned int groups = 0;

    if (attr_table[CTRL_ATTR_FAMILY_NAME] != NULL) {
        name = RTA_DATA(attr_table[CTRL_ATTR_FAMILY_NAME]);
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
    if (nl_init_seq_helper() < 0) {
        LOG_ERRNO_HELPER("nl_init_seq_helper()");
        return EXIT_FAILURE;
    }

    int exit_status = EXIT_FAILURE;
    int fd = nl_open_helper(NETLINK_GENERIC);
    if (fd < 0) {
        LOG_ERRNO_HELPER("nl_open_helper(NETLINK_GENERIC)");
        goto out_destroy_seq;
    }

    unsigned int seq = nl_get_next_seq_helper();
    struct genl_request request = { 0 };

    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(request.genl));
    request.nl_header.nlmsg_type = GENL_ID_CTRL;
    request.nl_header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.nl_header.nlmsg_seq = seq;
    request.genl.cmd = CTRL_CMD_GETFAMILY;
    request.genl.version = 1;

    if (nl_send_helper(fd, &request, request.nl_header.nlmsg_len) < 0) {
        LOG_ERRNO_HELPER("nl_send_helper()");
        goto out_close_fd;
    }

    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    unsigned int families = 0;
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
                LOG_MSG_HELPER("dump raced with a family change, result is incomplete");
                goto out_close_fd;
            }
            if (nl_header->nlmsg_type == NLMSG_DONE) {
                if (nl_check_done_helper(nl_header) < 0) {
                    LOG_ERRNO_HELPER("CTRL_CMD_GETFAMILY");
                    goto out_close_fd;
                }
                is_done = 1;
                break;
            }
            if (nl_check_error_helper(nl_header) < 0) {
                LOG_ERRNO_HELPER("CTRL_CMD_GETFAMILY");
                goto out_close_fd;
            }
            if (nl_header->nlmsg_type == GENL_ID_CTRL) {
                print_family_helper(nl_header);
                ++families;
            }
        }
    }

    LOG_RESULT_HELPER("%u generic netlink families", families);
    exit_status = 0;

out_close_fd:
    close(fd);
out_destroy_seq:
    nl_destroy_seq_helper();
    return exit_status;
}
