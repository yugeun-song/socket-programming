#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5001
#define BACKLOG_HELPER 8

int main(int argc, char **argv)
{
    char log_buf[ADDR_TEXT_MAX_HELPER] = { 0 };
    struct sockaddr_in peer_addr = { 0 };
    sigset_t wait_signal_mask;
    struct stat file_info;
    off_t offset = 0;
    ssize_t sent_bytes;
    int exit_status = 1;
    int file_fd = -1;
    int server_fd = -1;
    int client_fd = -1;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 1;
    }

    if (catch_shutdown_signals_helper(&wait_signal_mask) < 0) {
        LOG_ERRNO_HELPER("catch_shutdown_signals_helper()");
        return 1;
    }
    if (ignore_sigpipe_helper() < 0) {
        LOG_ERRNO_HELPER("sigaction(SIGPIPE)");
        return 1;
    }

    file_fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (file_fd < 0) {
        LOG_ERRNO_HELPER("open()");
        return 1;
    }

    if (fstat(file_fd, &file_info) < 0) {
        LOG_ERRNO_HELPER("fstat()");
        goto cleanup;
    }

    server_fd = tcp_listen_helper(PORT_HELPER, BACKLOG_HELPER);
    if (server_fd < 0) {
        LOG_ERRNO_HELPER("tcp_listen_helper()");
        goto cleanup;
    }

    LOG_MSG_HELPER("serving %s (%lld bytes) on port %d", argv[1], (long long)file_info.st_size, PORT_HELPER);

    client_fd = tcp_accept_helper(server_fd, &peer_addr, &wait_signal_mask);
    if (client_fd < 0) {
        if (should_shut_down_helper()) {
            LOG_MSG_HELPER("stopped by signal %d", which_shutdown_signal_helper());
            exit_status = 0;
        } else {
            LOG_ERRNO_HELPER("tcp_accept_helper()");
        }
        goto cleanup;
    }
    if (format_addr_helper(&peer_addr, log_buf, sizeof(log_buf)) < 0) {
        LOG_ERRNO_HELPER("format_addr_helper()");
        goto cleanup;
    }

    LOG_MSG_HELPER("accepted %s", log_buf);

    while (offset < file_info.st_size) {
        if (wait_ready_or_shutdown_helper(client_fd, POLLOUT, &wait_signal_mask) < 0) {
            if (should_shut_down_helper()) {
                LOG_MSG_HELPER("stopped by signal %d", which_shutdown_signal_helper());
            } else if (errno == ETIMEDOUT) {
                LOG_MSG_HELPER("%s stopped answering, TCP gave up on the connection", log_buf);
            } else {
                LOG_ERRNO_HELPER("wait_ready_or_shutdown_helper()");
            }
            break;
        }

        sent_bytes = sendfile(client_fd, file_fd, &offset, (size_t)(file_info.st_size - offset));
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

cleanup:
    if (client_fd >= 0) {
        close(client_fd);
    }
    if (server_fd >= 0) {
        close(server_fd);
    }
    if (file_fd >= 0) {
        close(file_fd);
    }
    return exit_status;
}