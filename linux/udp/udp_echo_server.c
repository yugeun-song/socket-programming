#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5000
#define BUF_SIZE_HELPER 1024

static int send_reply_helper(int fd, const void *const send_buf, size_t len,
                             const struct sockaddr_in *const peer_addr,
                             const sigset_t *const wait_signal_mask_nullable)
{
    for (;;) {
        if (wait_ready_or_shutdown_helper(fd, POLLOUT, wait_signal_mask_nullable) < 0) {
            return -1;
        }
        if (sendto(fd, send_buf, len, 0, (const struct sockaddr *)peer_addr,
                   sizeof(*peer_addr)) >= 0) {
            return 0;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
    }
}

int main(void)
{
    sigset_t wait_signal_mask;

    if (catch_shutdown_signals_helper(&wait_signal_mask) < 0) {
        LOG_ERRNO_HELPER("catch_shutdown_signals_helper()");
        return EXIT_FAILURE;
    }

    int fd = udp_bind_helper(PORT_HELPER);
    if (fd < 0) {
        LOG_ERRNO_HELPER("udp_bind_helper()");
        return EXIT_FAILURE;
    }

    LOG_MSG_HELPER("listening on port %d", PORT_HELPER);

    char log_buf[ADDR_TEXT_MAX_HELPER] = { 0 };
    struct sockaddr_in peer_addr = { 0 };
    char recv_buf[BUF_SIZE_HELPER];
    size_t buf_len = sizeof(recv_buf);

    for (;;) {
        ssize_t received_bytes = recvfrom_or_shutdown_helper(fd, recv_buf, buf_len, &peer_addr,
                                                             &wait_signal_mask);
        if (received_bytes < 0) {
            if (should_shut_down_helper() != 0) {
                LOG_MSG_HELPER("stopped by signal %d", get_shutdown_signal_helper());
            } else {
                LOG_ERRNO_HELPER("recvfrom()");
            }
            break;
        }
        if (format_addr_helper(log_buf, sizeof(log_buf), &peer_addr) < 0) {
            LOG_ERRNO_HELPER("format_addr_helper()");
            break;
        }

        LOG_RESULT_HELPER("received %zd bytes from %s", received_bytes, log_buf);

        if (send_reply_helper(fd, recv_buf, (size_t)received_bytes, &peer_addr,
                              &wait_signal_mask) < 0) {
            if (should_shut_down_helper() != 0) {
                LOG_MSG_HELPER("stopped by signal %d", get_shutdown_signal_helper());
                break;
            }
            LOG_ERRNO_HELPER("sendto()");
        }
    }

    close(fd);
    return 0;
}
