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

#define PORT 5002
#define CHUNK_SIZE (256 * 1024)
#define CHUNK_COUNT 16
#define DRAIN_TIMEOUT_MS 2000

struct zc_stats {
    unsigned int completed;
    unsigned int copied;
};

static int drain_completions(int fd, int timeout_ms, struct zc_stats *stats)
{
    union {
        char raw[CMSG_SPACE(sizeof(struct sock_extended_err) + sizeof(struct sockaddr_in))];
        struct cmsghdr align;
    } control;
    struct sock_extended_err serr;
    struct pollfd pfd = { 0 };
    struct msghdr msg = { 0 };
    struct cmsghdr *cmsg;
    unsigned int range;
    int ready;

    pfd.fd = fd;
    pfd.events = POLLERR;

    while (1) {
        ready = poll(&pfd, 1, timeout_ms);
        if (ready >= 0) {
            break;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
    if (ready == 0) {
        return 0;
    }

    msg.msg_control = control.raw;
    msg.msg_controllen = sizeof(control.raw);

    while (1) {
        if (recvmsg(fd, &msg, MSG_ERRQUEUE) >= 0) {
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        if (errno != EINTR) {
            return -1;
        }
    }

    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL || cmsg->cmsg_level != IPPROTO_IP || cmsg->cmsg_type != IP_RECVERR) {
        errno = EPROTO;
        return -1;
    }

    memcpy(&serr, CMSG_DATA(cmsg), sizeof(serr));
    if (serr.ee_errno != 0 || serr.ee_origin != SO_EE_ORIGIN_ZEROCOPY) {
        errno = EPROTO;
        return -1;
    }

    range = serr.ee_data - serr.ee_info + 1;
    stats->completed += range;
    if (serr.ee_code & SO_EE_CODE_ZEROCOPY_COPIED) {
        stats->copied += range;
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    struct zc_stats stats = { 0 };
    unsigned long long sent = 0;
    unsigned int pending = 0;
    int failed = 0;
    int enable = 1;
    char *chunk;
    size_t off;
    ssize_t n;
    int drained;
    int fd;
    int i;

    chunk = malloc(CHUNK_SIZE);
    if (chunk == NULL) {
        log_errno("malloc()");
        return 1;
    }
    memset(chunk, 'z', CHUNK_SIZE);

    fd = tcp_connect(host, PORT);
    if (fd < 0) {
        log_errno("tcp_connect()");
        free(chunk);
        return 1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable)) < 0) {
        log_errno("setsockopt(SO_ZEROCOPY)");
        close(fd);
        free(chunk);
        return 1;
    }

    for (i = 0; i < CHUNK_COUNT && !failed; ++i) {
        off = 0;
        while (off < CHUNK_SIZE) {
            n = send(fd, chunk + off, CHUNK_SIZE - off, MSG_ZEROCOPY | MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                log_errno("send()");
                failed = 1;
                break;
            }
            off += (size_t)n;
            ++pending;
        }
        sent += off;

        if (drain_completions(fd, 0, &stats) < 0) {
            log_errno("recvmsg(MSG_ERRQUEUE)");
            failed = 1;
        }
    }

    while (stats.completed < pending) {
        drained = drain_completions(fd, DRAIN_TIMEOUT_MS, &stats);
        if (drained < 0) {
            log_errno("recvmsg(MSG_ERRQUEUE)");
            break;
        }
        if (drained == 0) {
            log_msg("%u notifications missing", pending - stats.completed);
            break;
        }
    }

    printf("msg_zerocopy_client: sent %llu bytes in %u zero-copy send() calls\n", sent, pending);
    printf("msg_zerocopy_client: %u notifications, %u ranges the kernel copied\n", stats.completed, stats.copied);

    close(fd);
    free(chunk);
    return failed ? 1 : 0;
}