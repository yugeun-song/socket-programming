#define _GNU_SOURCE

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <linux/tcp.h>

#include "common/deadline.h"
#include "common/log.h"
#include "common/net_util.h"

#define PORT 5002
#define BACKLOG 8
#define MAP_SIZE (1024 * 1024)
#define BUF_SIZE 65536
#define IDLE_TIMEOUT_MS 5000

static unsigned long long sum_bytes(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    unsigned long long sum = 0;
    size_t i;

    for (i = 0; i < len; ++i) {
        sum += p[i];
    }
    return sum;
}

int main(void)
{
    char peer[PEER_TEXT_MAX] = { 0 };
    struct sockaddr_in addr = { 0 };
    struct tcp_zerocopy_receive zc;
    unsigned long long checksum = 0;
    unsigned long long mapped = 0;
    unsigned long long copied = 0;
    struct deadline accept_dl;
    struct deadline io_dl;
    sigset_t saved_mask;
    char buf[BUF_SIZE];
    socklen_t zclen;
    size_t want;
    ssize_t n;
    void *area;
    int listen_fd;
    int client_fd;

    if (install_stop_handlers(&saved_mask) < 0) {
        LOG_ERRNO("install_stop_handlers()");
        return 1;
    }

    listen_fd = tcp_listen(PORT, BACKLOG, 0, 0, NULL, 0);
    if (listen_fd < 0) {
        LOG_ERRNO("tcp_listen()");
        return 1;
    }

    LOG_MSG("listening on port %d", PORT);

    if (deadline_start(&accept_dl, DEADLINE_FOREVER) < 0) {
        LOG_ERRNO("clock_gettime()");
        close(listen_fd);
        return 1;
    }

    client_fd = tcp_accept(listen_fd, &addr, &accept_dl, &saved_mask);
    if (client_fd < 0) {
        LOG_ERRNO("tcp_accept()");
        close(listen_fd);
        return 1;
    }
    if (format_addr(&addr, peer, sizeof(peer)) < 0) {
        LOG_ERRNO("format_addr()");
        close(client_fd);
        close(listen_fd);
        return 1;
    }

    LOG_MSG("accepted %s", peer);

    area = mmap(NULL, MAP_SIZE, PROT_READ, MAP_SHARED, client_fd, 0);
    if (area == MAP_FAILED) {
        LOG_ERRNO("mmap()");
        close(client_fd);
        close(listen_fd);
        return 1;
    }

    while (1) {
        if (deadline_start(&io_dl, IDLE_TIMEOUT_MS) < 0) {
            LOG_ERRNO("clock_gettime()");
            break;
        }
        if (wait_ready(client_fd, POLLIN, &io_dl, &saved_mask) < 0) {
            LOG_ERRNO("wait_ready()");
            break;
        }

        memset(&zc, 0, sizeof(zc));
        zclen = sizeof(zc);
        zc.address = (uint64_t)(uintptr_t)area;
        zc.length = MAP_SIZE;

        if (getsockopt(client_fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, &zc, &zclen) < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EIO) {
                break;
            }
            LOG_ERRNO("getsockopt(TCP_ZEROCOPY_RECEIVE)");
            break;
        }

        if (zc.length > 0) {
            checksum += sum_bytes(area, zc.length);
            mapped += zc.length;
            madvise(area, zc.length, MADV_DONTNEED);
            continue;
        }

        want = sizeof(buf);
        if (zc.recv_skip_hint > 0 && zc.recv_skip_hint < want) {
            want = zc.recv_skip_hint;
        }

        n = recv_until(client_fd, buf, want, &io_dl, &saved_mask);
        if (n < 0) {
            LOG_ERRNO("recv()");
            break;
        }
        if (n == 0) {
            break;
        }
        checksum += sum_bytes(buf, (size_t)n);
        copied += (unsigned long long)n;
    }

    printf("zerocopy_recv_server: %llu bytes mapped, %llu bytes copied, checksum %llu, from %s\n", mapped, copied, checksum, peer);

    munmap(area, MAP_SIZE);
    close(client_fd);
    close(listen_fd);
    return 0;
}