#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/sock_diag.h>

#include "common/log.h"
#include "netlink/nl_util.h"

static const char *const state_names[13] = { "?",         "ESTABLISHED", "SYN_SENT",    "SYN_RECV",   "FIN_WAIT1",
                                             "FIN_WAIT2", "TIME_WAIT",   "CLOSE",       "CLOSE_WAIT", "LAST_ACK",
                                             "LISTEN",    "CLOSING",     "NEW_SYN_RECV" };

struct diag_request {
    struct nlmsghdr nl_header;
    struct inet_diag_req_v2 diag;
};

static void print_socket_helper(struct nlmsghdr *nl_header)
{
    char src_text[INET6_ADDRSTRLEN] = { 0 };
    char dst_text[INET6_ADDRSTRLEN] = { 0 };
    struct inet_diag_msg *diag_msg;
    const char *state = "?";

    if (nl_header->nlmsg_len < NLMSG_LENGTH(sizeof(*diag_msg))) {
        return;
    }

    diag_msg = (struct inet_diag_msg *)NLMSG_DATA(nl_header);
    if (inet_ntop(diag_msg->idiag_family, diag_msg->id.idiag_src, src_text, sizeof(src_text)) == NULL) {
        return;
    }
    if (inet_ntop(diag_msg->idiag_family, diag_msg->id.idiag_dst, dst_text, sizeof(dst_text)) == NULL) {
        return;
    }
    if (diag_msg->idiag_state < sizeof(state_names) / sizeof(state_names[0])) {
        state = state_names[diag_msg->idiag_state];
    }

    LOG_RESULT_HELPER("%-12s %15s:%-5u %15s:%-5u uid %-5u inode %u", state, src_text, ntohs(diag_msg->id.idiag_sport),
                      dst_text, ntohs(diag_msg->id.idiag_dport), diag_msg->idiag_uid, diag_msg->idiag_inode);
}

int main(void)
{
    struct diag_request request = { 0 };
    char nl_recv_buf[NL_BUF_SIZE_HELPER];
    struct nlmsghdr *nl_header;
    unsigned int seq;
    unsigned int sockets = 0;
    ssize_t received_bytes;
    int is_done = 0;
    int remaining;
    int fd;

    fd = nl_open_helper(NETLINK_SOCK_DIAG);
    if (fd < 0) {
        LOG_ERRNO_HELPER("nl_open_helper(NETLINK_SOCK_DIAG)");
        return 1;
    }

    seq = nl_next_seq_helper();
    request.nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(request.diag));
    request.nl_header.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    request.nl_header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.nl_header.nlmsg_seq = seq;
    request.diag.sdiag_family = AF_INET;
    request.diag.sdiag_protocol = IPPROTO_TCP;
    request.diag.idiag_states = ~0U;

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
                LOG_MSG_HELPER("dump raced with a socket change, result is incomplete");
                close(fd);
                return 1;
            }
            if (nl_header->nlmsg_type == NLMSG_DONE) {
                if (nl_check_done_helper(nl_header) < 0) {
                    LOG_ERRNO_HELPER("SOCK_DIAG_BY_FAMILY");
                    close(fd);
                    return 1;
                }
                is_done = 1;
                break;
            }
            if (nl_check_error_helper(nl_header) < 0) {
                LOG_ERRNO_HELPER("SOCK_DIAG_BY_FAMILY");
                close(fd);
                return 1;
            }
            if (nl_header->nlmsg_type == SOCK_DIAG_BY_FAMILY) {
                print_socket_helper(nl_header);
                ++sockets;
            }
            nl_header = NLMSG_NEXT(nl_header, remaining);
        }
    }

    LOG_RESULT_HELPER("%u IPv4 TCP sockets", sockets);

    close(fd);
    return 0;
}