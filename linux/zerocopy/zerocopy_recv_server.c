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

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5002
#define BACKLOG_HELPER 8
#define MAP_SIZE_HELPER (1024 * 1024)
#define BUF_SIZE_HELPER 65536

int main(void)
{
    char log_buf[ADDR_TEXT_MAX_HELPER] = { 0 };
    struct sockaddr_in peer_addr = { 0 };
    struct tcp_zerocopy_receive zerocopy;
    unsigned long long mapped = 0;
    unsigned long long copied = 0;
    sigset_t wait_signal_mask;
    char recv_buf[BUF_SIZE_HELPER];
    socklen_t zerocopy_len;
    size_t copy_len;
    ssize_t received_bytes;
    void *mapped_area;
    int server_fd;
    int client_fd;

    if (catch_shutdown_signals_helper(&wait_signal_mask) < 0) {
        LOG_ERRNO_HELPER("catch_shutdown_signals_helper()");
        return 1;
    }

    server_fd = tcp_listen_helper(PORT_HELPER, BACKLOG_HELPER);
    if (server_fd < 0) {
        LOG_ERRNO_HELPER("tcp_listen_helper()");
        return 1;
    }

    LOG_MSG_HELPER("listening on port %d", PORT_HELPER);

    client_fd = tcp_accept_helper(server_fd, &peer_addr, &wait_signal_mask);
    if (client_fd < 0) {
        if (should_shut_down_helper()) {
            LOG_MSG_HELPER("stopped by signal %d", which_shutdown_signal_helper());
            close(server_fd);
            return 0;
        }
        LOG_ERRNO_HELPER("tcp_accept_helper()");
        close(server_fd);
        return 1;
    }
    if (format_addr_helper(&peer_addr, log_buf, sizeof(log_buf)) < 0) {
        LOG_ERRNO_HELPER("format_addr_helper()");
        close(client_fd);
        close(server_fd);
        return 1;
    }

    LOG_MSG_HELPER("accepted %s", log_buf);

    mapped_area = mmap(NULL, MAP_SIZE_HELPER, PROT_READ, MAP_SHARED, client_fd, 0);
    if (mapped_area == MAP_FAILED) {
        LOG_ERRNO_HELPER("mmap()");
        close(client_fd);
        close(server_fd);
        return 1;
    }

    while (1) {
        if (wait_ready_or_shutdown_helper(client_fd, POLLIN, &wait_signal_mask) < 0) {
            if (should_shut_down_helper()) {
                LOG_MSG_HELPER("stopped by signal %d", which_shutdown_signal_helper());
            } else if (errno == ETIMEDOUT) {
                LOG_MSG_HELPER("%s stopped answering, TCP gave up on the connection", log_buf);
            } else {
                LOG_ERRNO_HELPER("wait_ready_or_shutdown_helper()");
            }
            break;
        }

        memset(&zerocopy, 0, sizeof(zerocopy));
        zerocopy_len = sizeof(zerocopy);
        zerocopy.address = (uint64_t)(uintptr_t)mapped_area;
        zerocopy.length = MAP_SIZE_HELPER;

        if (getsockopt(client_fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, &zerocopy, &zerocopy_len) < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == EIO) {
                break;
            }
            LOG_ERRNO_HELPER("getsockopt(TCP_ZEROCOPY_RECEIVE)");
            break;
        }

        if (zerocopy.length > 0) {
            mapped += zerocopy.length;
            madvise(mapped_area, zerocopy.length, MADV_DONTNEED);
            continue;
        }

        copy_len = sizeof(recv_buf);
        if (zerocopy.recv_skip_hint > 0 && zerocopy.recv_skip_hint < copy_len) {
            copy_len = zerocopy.recv_skip_hint;
        }

        received_bytes = recv_or_shutdown_helper(client_fd, recv_buf, copy_len, &wait_signal_mask);
        if (received_bytes < 0) {
            if (should_shut_down_helper()) {
                LOG_MSG_HELPER("stopped by signal %d", which_shutdown_signal_helper());
            } else if (errno == ETIMEDOUT) {
                LOG_MSG_HELPER("%s stopped answering, TCP gave up on the connection", log_buf);
            } else {
                LOG_ERRNO_HELPER("recv()");
            }
            break;
        } else if (received_bytes == 0) {
            break;
        }
        copied += (unsigned long long)received_bytes;
    }

    LOG_RESULT_HELPER("%llu bytes the kernel mapped, %llu it had to copy, from %s", mapped, copied, log_buf);

    munmap(mapped_area, MAP_SIZE_HELPER);
    close(client_fd);
    close(server_fd);
    return 0;
}