#define _GNU_SOURCE

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <linux/tcp.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT 5002
#define BACKLOG 8
#define MAP_SIZE (1024 * 1024)
#define BUF_SIZE 65536

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
    struct tcp_zerocopy_receive zc;
    unsigned long long checksum = 0;
    unsigned long long mapped = 0;
    unsigned long long copied = 0;
    char buf[BUF_SIZE];
    socklen_t zclen;
    size_t want;
    ssize_t n;
    void *area;
    int listen_fd;
    int client_fd;

    listen_fd = tcp_listen(PORT, BACKLOG, 0, 0, NULL, 0);
    if (listen_fd < 0) {
        log_errno("tcp_listen()");
        return 1;
    }

    log_msg("listening on port %d", PORT);

    client_fd = tcp_accept(listen_fd);
    if (client_fd < 0) {
        log_errno("tcp_accept()");
        close(listen_fd);
        return 1;
    }

    area = mmap(NULL, MAP_SIZE, PROT_READ, MAP_SHARED, client_fd, 0);
    if (area == MAP_FAILED) {
        log_errno("mmap()");
        close(client_fd);
        close(listen_fd);
        return 1;
    }

    while (1) {
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
            log_errno("getsockopt(TCP_ZEROCOPY_RECEIVE)");
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

        n = recv(client_fd, buf, want, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_errno("recv()");
            break;
        }
        if (n == 0) {
            break;
        }
        checksum += sum_bytes(buf, (size_t)n);
        copied += (unsigned long long)n;
    }

    printf("zerocopy_recv_server: %llu bytes mapped, %llu bytes copied, checksum %llu\n", mapped, copied, checksum);

    munmap(area, MAP_SIZE);
    close(client_fd);
    close(listen_fd);
    return 0;
}