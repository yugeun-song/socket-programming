#define _GNU_SOURCE

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include <linux/errqueue.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5002
#define CHUNK_SIZE_HELPER (256 * 1024)
#define CHUNK_COUNT_HELPER 16

union zc_control {
    char raw[CMSG_SPACE(sizeof(struct sock_extended_err) + sizeof(struct sockaddr_in))];
    struct cmsghdr align;
};

struct zc_stats {
    unsigned int completed;
    unsigned int copied;
};

static int drain_completions_helper(int fd, int should_wait, const sigset_t *wait_signal_mask, struct zc_stats *stats)
{
    struct sock_extended_err sock_err;
    struct msghdr msg = { 0 };
    union zc_control control;
    struct cmsghdr *cmsg;
    unsigned int copied_range;

    if (should_wait && wait_ready_or_shutdown_helper(fd, POLLERR, wait_signal_mask) < 0) {
        return -1;
    }

    msg.msg_control = control.raw;
    msg.msg_controllen = sizeof(control.raw);

    while (1) {
        if (recvmsg(fd, &msg, MSG_ERRQUEUE) >= 0) {
            break;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        } else if (errno != EINTR) {
            return -1;
        }
    }

    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL || cmsg->cmsg_level != IPPROTO_IP || cmsg->cmsg_type != IP_RECVERR) {
        errno = EPROTO;
        return -1;
    }

    memcpy(&sock_err, CMSG_DATA(cmsg), sizeof(sock_err));
    if (sock_err.ee_errno != 0 || sock_err.ee_origin != SO_EE_ORIGIN_ZEROCOPY) {
        errno = EPROTO;
        return -1;
    }

    copied_range = sock_err.ee_data - sock_err.ee_info + 1;
    stats->completed += copied_range;
    if (sock_err.ee_code & SO_EE_CODE_ZEROCOPY_COPIED) {
        stats->copied += copied_range;
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    char log_buf[ADDR_TEXT_MAX_HELPER] = { 0 };
    struct zc_stats stats = { 0 };
    unsigned long long sent = 0;
    unsigned int pending = 0;
    int has_failed = 0;
    int zerocopy_enable = 1;
    char *send_buf;
    size_t offset;
    ssize_t sent_bytes;
    int drain_status;
    int fd;
    int i;

    send_buf = malloc(CHUNK_SIZE_HELPER);
    if (send_buf == NULL) {
        LOG_ERRNO_HELPER("malloc()");
        return 1;
    }
    memset(send_buf, 'z', CHUNK_SIZE_HELPER);

    fd = tcp_connect_helper(host, PORT_HELPER);
    if (fd < 0) {
        LOG_ERRNO_HELPER("tcp_connect_helper()");
        free(send_buf);
        return 1;
    }
    if (format_peer_helper(fd, log_buf, sizeof(log_buf)) < 0) {
        LOG_ERRNO_HELPER("format_peer_helper()");
        close(fd);
        free(send_buf);
        return 1;
    }

    LOG_MSG_HELPER("connected to %s", log_buf);

    if (setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &zerocopy_enable, sizeof(zerocopy_enable)) < 0) {
        LOG_ERRNO_HELPER("setsockopt(SO_ZEROCOPY)");
        close(fd);
        free(send_buf);
        return 1;
    }

    for (i = 0; i < CHUNK_COUNT_HELPER && !has_failed; ++i) {
        offset = 0;
        while (offset < CHUNK_SIZE_HELPER) {
            if (wait_ready_or_shutdown_helper(fd, POLLOUT, NULL) < 0) {
                LOG_ERRNO_HELPER("wait_ready_or_shutdown_helper()");
                has_failed = 1;
                break;
            }

            sent_bytes = send(fd, send_buf + offset, CHUNK_SIZE_HELPER - offset, MSG_ZEROCOPY | MSG_NOSIGNAL);
            if (sent_bytes < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                LOG_ERRNO_HELPER("send()");
                has_failed = 1;
                break;
            }
            offset += (size_t)sent_bytes;
            ++pending;
        }
        sent += offset;

        if (drain_completions_helper(fd, 0, NULL, &stats) < 0) {
            LOG_ERRNO_HELPER("recvmsg(MSG_ERRQUEUE)");
            has_failed = 1;
        }
    }

    while (stats.completed < pending) {
        drain_status = drain_completions_helper(fd, 1, NULL, &stats);
        if (drain_status < 0) {
            LOG_ERRNO_HELPER("recvmsg(MSG_ERRQUEUE)");
            break;
        } else if (drain_status == 0) {
            LOG_MSG_HELPER("%u notifications missing", pending - stats.completed);
            break;
        }
    }

    LOG_RESULT_HELPER("sent %llu bytes to %s in %u zero-copy send() calls", sent, log_buf, pending);
    LOG_RESULT_HELPER("%u notifications, %u ranges the kernel copied", stats.completed, stats.copied);

    close(fd);
    free(send_buf);
    return has_failed ? 1 : 0;
}