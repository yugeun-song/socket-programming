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

#define DUMP_TIMEOUT_MS 5000

struct diag_request {
    struct nlmsghdr nlh;
    struct inet_diag_req_v2 diag;
};

static void print_socket(struct nlmsghdr *nlh)
{
    char src[INET6_ADDRSTRLEN] = { 0 };
    char dst[INET6_ADDRSTRLEN] = { 0 };
    struct inet_diag_msg *msg;
    const char *state = "?";

    if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*msg))) {
        return;
    }

    msg = (struct inet_diag_msg *)NLMSG_DATA(nlh);
    if (inet_ntop(msg->idiag_family, msg->id.idiag_src, src, sizeof(src)) == NULL) {
        return;
    }
    if (inet_ntop(msg->idiag_family, msg->id.idiag_dst, dst, sizeof(dst)) == NULL) {
        return;
    }
    if (msg->idiag_state < sizeof(state_names) / sizeof(state_names[0])) {
        state = state_names[msg->idiag_state];
    }

    printf("sock_diag_dump: %-12s %15s:%-5u %15s:%-5u uid %-5u inode %u\n", state, src, ntohs(msg->id.idiag_sport), dst,
           ntohs(msg->id.idiag_dport), msg->idiag_uid, msg->idiag_inode);
}

int main(void)
{
    struct diag_request req = { 0 };
    char buf[NL_BUF_SIZE];
    struct nlmsghdr *nlh;
    struct deadline dl;
    unsigned int seq;
    unsigned int sockets = 0;
    ssize_t n;
    int is_done = 0;
    int len;
    int fd;

    fd = nl_open(NETLINK_SOCK_DIAG);
    if (fd < 0) {
        log_errno("nl_open(NETLINK_SOCK_DIAG)");
        return 1;
    }

    seq = nl_next_seq();
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(req.diag));
    req.nlh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = seq;
    req.diag.sdiag_family = AF_INET;
    req.diag.sdiag_protocol = IPPROTO_TCP;
    req.diag.idiag_states = ~0U;

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
                log_msg("dump raced with a socket change, result is incomplete");
                close(fd);
                return 1;
            }
            if (nlh->nlmsg_type == NLMSG_DONE) {
                if (nl_check_done(nlh) < 0) {
                    log_errno("SOCK_DIAG_BY_FAMILY");
                    close(fd);
                    return 1;
                }
                is_done = 1;
                break;
            }
            if (nl_check_error(nlh) < 0) {
                log_errno("SOCK_DIAG_BY_FAMILY");
                close(fd);
                return 1;
            }
            if (nlh->nlmsg_type == SOCK_DIAG_BY_FAMILY) {
                print_socket(nlh);
                ++sockets;
            }
            nlh = NLMSG_NEXT(nlh, len);
        }
    }

    printf("sock_diag_dump: %u IPv4 TCP sockets\n", sockets);

    close(fd);
    return 0;
}