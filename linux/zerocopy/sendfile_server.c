#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5001
#define BACKLOG_HELPER 8

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    sigset_t wait_signal_mask;

    if (catch_shutdown_signals_helper(&wait_signal_mask) < 0) {
        LOG_ERRNO_HELPER("catch_shutdown_signals_helper()");
        return EXIT_FAILURE;
    }
    if (ignore_sigpipe_helper() < 0) {
        LOG_ERRNO_HELPER("sigaction(SIGPIPE)");
        return EXIT_FAILURE;
    }

    int file_fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (file_fd < 0) {
        LOG_ERRNO_HELPER("open()");
        return EXIT_FAILURE;
    }

    int exit_status = EXIT_FAILURE;
    struct stat file_info;

    if (fstat(file_fd, &file_info) < 0) {
        LOG_ERRNO_HELPER("fstat()");
        goto out_close_file;
    }

    int server_fd = tcp_listen_helper(PORT_HELPER, BACKLOG_HELPER);
    if (server_fd < 0) {
        LOG_ERRNO_HELPER("tcp_listen_helper()");
        goto out_close_file;
    }

    LOG_MSG_HELPER("serving %s (%lld bytes) on port %d", argv[1], (long long)file_info.st_size,
                   PORT_HELPER);

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

    off_t offset = 0;

    while (offset < file_info.st_size) {
        if (wait_ready_or_shutdown_helper(client_fd, POLLOUT, &wait_signal_mask) < 0) {
            if (should_shut_down_helper() != 0) {
                LOG_MSG_HELPER("stopped by signal %d", get_shutdown_signal_helper());
            } else if (errno == ETIMEDOUT) {
                LOG_MSG_HELPER("%s stopped answering, TCP gave up on the connection", log_buf);
            } else {
                LOG_ERRNO_HELPER("wait_ready_or_shutdown_helper()");
            }
            break;
        }

        ssize_t sent_bytes = sendfile(client_fd, file_fd, &offset,
                                      (size_t)(file_info.st_size - offset));
        if (sent_bytes < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            LOG_ERRNO_HELPER("sendfile()");
            break;
        } else if (sent_bytes == 0) {
            break;
        }
    }

    LOG_RESULT_HELPER("sent %lld bytes to %s", (long long)offset, log_buf);
    exit_status = 0;

out_close_client:
    close(client_fd);
out_close_server:
    close(server_fd);
out_close_file:
    close(file_fd);
    return exit_status;
}
