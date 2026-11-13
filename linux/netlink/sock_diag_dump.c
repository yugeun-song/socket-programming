#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/sock_diag.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/log.h"
#include "netlink/nl_util.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct diag_request {
    struct nlmsghdr nl_header;
    struct inet_diag_req_v2 diag;
};

static const char *const gs_state_names[13] = {
    "?",
    "ESTABLISHED",
    "SYN_SENT",
    "SYN_RECV",
    "FIN_WAIT1",
    "FIN_WAIT2",
    "TIME_WAIT",
    "CLOSE",
    "CLOSE_WAIT",
    "LAST_ACK",
    "LISTEN",
    "CLOSING",
    "NEW_SYN_RECV",
};

static void print_socket_helper(const struct nlmsghdr *const nl_header)
{
    const struct inet_diag_msg *diag_msg = NLMSG_DATA(nl_header);

    if (nl_header->nlmsg_len < NLMSG_LENGTH(sizeof(*diag_msg))) {
        return;
    }

    char src_text[INET6_ADDRSTRLEN] = { 0 };
    char dst_text[INET6_ADDRSTRLEN] = { 0 };

    if (inet_ntop(diag_msg->idiag_family, diag_msg->id.idiag_src, src_text,
                  sizeof(src_text)) == NULL) {
        return;
    }
    if (inet_ntop(diag_msg->idiag_family, diag_msg->id.idiag_dst, dst_text,
                  sizeof(dst_text)) == NULL) {
        return;
    }

    const char *state = "?";

    if (diag_msg->idiag_state < ARRAY_SIZE(gs_state_names)) {
        state = gs_state_names[diag_msg->idiag_state];
    }

    LOG_RESULT_HELPER("%-12s %15s:%-5u %15s:%-5u uid %-5u inode %u", state, src_text,
                      ntohs(diag_msg->id.idiag_sport), dst_text, ntohs(diag_msg->id.idiag_dport),
                      diag_msg->idiag_uid, diag_msg->idiag_inode);
}

int main(void)
{
    int fd = nl_open_helper(NETLINK_SOCK_DIAG);
    if (fd < 0) {
        LOG_ERRNO_HELPER("nl_open_helper(NETLINK_SOCK_DIAG)");
        return EXIT_FAILURE;
    }

    int exit_status = EXIT_FAILURE;
    unsigned int seq = nl_get_next_seq_helper();
    struct diag_request request = { 0 };

    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(request.diag));
    request.nl_header.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    request.nl_header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.nl_header.nlmsg_seq = seq;
    request.diag.sdiag_family = AF_INET;
    request.diag.sdiag_protocol = IPPROTO_TCP;
    request.diag.idiag_states = ~0U;

    if (nl_send_helper(fd, &request, request.nl_header.nlmsg_len) < 0) {
        LOG_ERRNO_HELPER("nl_send_helper()");
        goto out_close_fd;
    }

    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    unsigned int sockets = 0;
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
                LOG_MSG_HELPER("dump raced with a socket change, result is incomplete");
                goto out_close_fd;
            }
            if (nl_header->nlmsg_type == NLMSG_DONE) {
                if (nl_check_done_helper(nl_header) < 0) {
                    LOG_ERRNO_HELPER("SOCK_DIAG_BY_FAMILY");
                    goto out_close_fd;
                }
                is_done = 1;
                break;
            }
            if (nl_check_error_helper(nl_header) < 0) {
                LOG_ERRNO_HELPER("SOCK_DIAG_BY_FAMILY");
                goto out_close_fd;
            }
            if (nl_header->nlmsg_type == SOCK_DIAG_BY_FAMILY) {
                print_socket_helper(nl_header);
                ++sockets;
            }
        }
    }

    LOG_RESULT_HELPER("%u IPv4 TCP sockets", sockets);
    exit_status = 0;

out_close_fd:
    close(fd);
    return exit_status;
}
