#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5000
#define BACKLOG_HELPER 8
#define BUF_SIZE_HELPER 1024

int main(void)
{
    sigset_t wait_signal_mask;

    if (catch_shutdown_signals_helper(&wait_signal_mask) < 0) {
        LOG_ERRNO_HELPER("catch_shutdown_signals_helper()");
        return EXIT_FAILURE;
    }
    if (ignore_sigpipe_helper() < 0) {
        LOG_ERRNO_HELPER("sigaction(SIGPIPE)");
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

    char recv_buf[BUF_SIZE_HELPER];

    for (;;) {
        ssize_t received_bytes = recv_or_shutdown_helper(client_fd, recv_buf, sizeof(recv_buf),
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
            LOG_MSG_HELPER("%s closed the connection", log_buf);
            break;
        }

        LOG_RESULT_HELPER("received %zd bytes from %s", received_bytes, log_buf);

        size_t send_len = (size_t)received_bytes;
        int send_status = send_all_or_shutdown_helper(client_fd, recv_buf, send_len,
                                                      &wait_signal_mask);
        if (send_status < 0) {
            if (should_shut_down_helper() != 0) {
                LOG_MSG_HELPER("stopped by signal %d", get_shutdown_signal_helper());
            } else if (errno == ETIMEDOUT) {
                LOG_MSG_HELPER("%s stopped answering, TCP gave up on the connection", log_buf);
            } else {
                LOG_ERRNO_HELPER("send()");
            }
            break;
        }
    }

    exit_status = 0;

out_close_client:
    close(client_fd);
out_close_server:
    close(server_fd);
    return exit_status;
}
