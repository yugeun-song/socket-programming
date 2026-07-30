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

#include "common/deadline.h"
#include "common/log.h"
#include "common/net_util.h"

#define PORT 5002
#define CHUNK_SIZE (256 * 1024)
#define CHUNK_COUNT 16
#define DRAIN_TIMEOUT_MS 2000
#define IDLE_TIMEOUT_MS 5000

union zc_control {
    char raw[CMSG_SPACE(sizeof(struct sock_extended_err) + sizeof(struct sockaddr_in))];
    struct cmsghdr align;
};

struct zc_stats {
    unsigned int completed;
    unsigned int copied;
};

static int drain_completions(int fd, const struct deadline *dl, struct zc_stats *stats)
{
    struct sock_extended_err serr;
    struct msghdr msg = { 0 };
    union zc_control control;
    struct cmsghdr *cmsg;
    unsigned int range;
    int ready;

    while (1) {
        ready = poll_until(fd, POLLERR, dl, NULL);
        if (ready >= 0) {
            break;
        }
        if (errno != EINTR) {
            return -1;
        }
        if (deadline_expired(dl)) {
            return 0;
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
    char peer[PEER_TEXT_MAX] = { 0 };
    struct zc_stats stats = { 0 };
    unsigned long long sent = 0;
    unsigned int pending = 0;
    struct deadline connect_dl;
    struct deadline probe_dl;
    struct deadline drain_dl;
    struct deadline send_dl;
    int has_failed = 0;
    int enable = 1;
    char *chunk;
    size_t off;
    ssize_t n;
    int drained;
    int fd;
    int i;

    chunk = malloc(CHUNK_SIZE);
    if (chunk == NULL) {
        LOG_ERRNO("malloc()");
        return 1;
    }
    memset(chunk, 'z', CHUNK_SIZE);

    if (deadline_start(&probe_dl, 0) < 0) {
        LOG_ERRNO("clock_gettime()");
        free(chunk);
        return 1;
    }

    if (deadline_start(&connect_dl, IDLE_TIMEOUT_MS) < 0) {
        LOG_ERRNO("clock_gettime()");
        free(chunk);
        return 1;
    }

    fd = tcp_connect(host, PORT, &connect_dl);
    if (fd < 0) {
        LOG_ERRNO("tcp_connect()");
        free(chunk);
        return 1;
    }
    if (format_peer(fd, peer, sizeof(peer)) < 0) {
        LOG_ERRNO("format_peer()");
        close(fd);
        free(chunk);
        return 1;
    }

    LOG_MSG("connected to %s", peer);

    if (setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable)) < 0) {
        LOG_ERRNO("setsockopt(SO_ZEROCOPY)");
        close(fd);
        free(chunk);
        return 1;
    }

    for (i = 0; i < CHUNK_COUNT && !has_failed; ++i) {
        off = 0;
        while (off < CHUNK_SIZE) {
            if (deadline_start(&send_dl, IDLE_TIMEOUT_MS) < 0) {
                LOG_ERRNO("clock_gettime()");
                has_failed = 1;
                break;
            }
            if (wait_ready(fd, POLLOUT, &send_dl, NULL) < 0) {
                LOG_ERRNO("wait_ready()");
                has_failed = 1;
                break;
            }

            n = send(fd, chunk + off, CHUNK_SIZE - off, MSG_ZEROCOPY | MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                LOG_ERRNO("send()");
                has_failed = 1;
                break;
            }
            off += (size_t)n;
            ++pending;
        }
        sent += off;

        if (drain_completions(fd, &probe_dl, &stats) < 0) {
            LOG_ERRNO("recvmsg(MSG_ERRQUEUE)");
            has_failed = 1;
        }
    }

    if (deadline_start(&drain_dl, DRAIN_TIMEOUT_MS) < 0) {
        LOG_ERRNO("clock_gettime()");
        close(fd);
        free(chunk);
        return 1;
    }

    while (stats.completed < pending) {
        drained = drain_completions(fd, &drain_dl, &stats);
        if (drained < 0) {
            LOG_ERRNO("recvmsg(MSG_ERRQUEUE)");
            break;
        }
        if (drained == 0) {
            LOG_MSG("%u notifications missing", pending - stats.completed);
            break;
        }
    }

    printf("msg_zerocopy_client: sent %llu bytes to %s in %u zero-copy send() calls\n", sent, peer, pending);
    printf("msg_zerocopy_client: %u notifications, %u ranges the kernel copied\n", stats.completed, stats.copied);

    close(fd);
    free(chunk);
    return has_failed ? 1 : 0;
}