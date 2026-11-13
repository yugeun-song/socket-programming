#define _DEFAULT_SOURCE

#include <errno.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5002
#define BACKLOG_HELPER 8
#define MAP_SIZE_HELPER (1024 * 1024)
#define BUF_SIZE_HELPER 65536

int main(void)
{
    sigset_t wait_signal_mask;

    if (catch_shutdown_signals_helper(&wait_signal_mask) < 0) {
        LOG_ERRNO_HELPER("catch_shutdown_signals_helper()");
        return EXIT_FAILURE;
    }

    int server_fd = tcp_listen_helper(PORT_HELPER, BACKLOG_HELPER);
    if (server_fd < 0) {
        LOG_ERRNO_HELPER("tcp_listen_helper()");
        return EXIT_FAILURE;
    }

    LOG_MSG_HELPER("listening on port %d", PORT_HELPER);

    int exit_status = EXIT_FAILURE;
    struct sockaddr_in peer_addr = { 0 };

    int client_fd = tcp_accept_helper(server_fd, &peer_addr, &wait_signal_mask);
    if (client_fd < 0) {
        if (should_shut_down_helper() != 0) {
            LOG_MSG_HELPER("stopped by signal %d", get_shutdown_signal_helper());
            exit_status = 0;
        } else {
            LOG_ERRNO_HELPER("tcp_accept_helper()");
        }
        goto out_close_server;
    }

    char log_buf[ADDR_TEXT_MAX_HELPER] = { 0 };

    if (format_addr_helper(log_buf, sizeof(log_buf), &peer_addr) < 0) {
        LOG_ERRNO_HELPER("format_addr_helper()");
        goto out_close_client;
    }

    LOG_MSG_HELPER("accepted %s", log_buf);

    void *mapped_area = mmap(NULL, MAP_SIZE_HELPER, PROT_READ, MAP_SHARED, client_fd, 0);
    if (mapped_area == MAP_FAILED) {
        LOG_ERRNO_HELPER("mmap()");
        goto out_close_client;
    }

    unsigned long long mapped = 0;
    unsigned long long copied = 0;
    char recv_buf[BUF_SIZE_HELPER];

    for (;;) {
        if (wait_ready_or_shutdown_helper(client_fd, POLLIN, &wait_signal_mask) < 0) {
            if (should_shut_down_helper() != 0) {
                LOG_MSG_HELPER("stopped by signal %d", get_shutdown_signal_helper());
            } else if (errno == ETIMEDOUT) {
                LOG_MSG_HELPER("%s stopped answering, TCP gave up on the connection", log_buf);
            } else {
                LOG_ERRNO_HELPER("wait_ready_or_shutdown_helper()");
            }
            break;
        }

        struct tcp_zerocopy_receive zerocopy;
        socklen_t zerocopy_len = sizeof(zerocopy);

        memset(&zerocopy, 0, sizeof(zerocopy));
        zerocopy.address = (uint64_t)((uintptr_t)mapped_area);
        zerocopy.length = MAP_SIZE_HELPER;

        if (getsockopt(client_fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, &zerocopy,
                       &zerocopy_len) < 0) {
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

        size_t copy_len = sizeof(recv_buf);

        if (zerocopy.recv_skip_hint > 0 && zerocopy.recv_skip_hint < copy_len) {
            copy_len = zerocopy.recv_skip_hint;
        }

        ssize_t received_bytes = recv_or_shutdown_helper(client_fd, recv_buf, copy_len,
                                                         &wait_signal_mask);
        if (received_bytes < 0) {
            if (should_shut_down_helper() != 0) {
                LOG_MSG_HELPER("stopped by signal %d", get_shutdown_signal_helper());
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

    LOG_RESULT_HELPER("%llu bytes the kernel mapped, %llu it had to copy, from %s", mapped, copied,
                      log_buf);
    exit_status = 0;

    munmap(mapped_area, MAP_SIZE_HELPER);
    mapped_area = NULL;
out_close_client:
    close(client_fd);
out_close_server:
    close(server_fd);
    return exit_status;
}
