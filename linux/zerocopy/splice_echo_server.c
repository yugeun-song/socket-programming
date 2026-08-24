#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5000
#define BACKLOG_HELPER 8
#define CHUNK_HELPER 65536
#define PIPE_READ_HELPER 0
#define PIPE_WRITE_HELPER 1

static int splice_all_helper(int in_fd, int out_fd, size_t len, const sigset_t *mask)
{
    ssize_t moved_bytes;

    while (len > 0) {
        if (wait_ready_or_shutdown_helper(out_fd, POLLOUT, mask) < 0) {
            return -1;
        }

        moved_bytes = splice(in_fd, NULL, out_fd, NULL, len, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
        if (moved_bytes < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        } else if (moved_bytes == 0) {
            return 1;
        }
        len -= (size_t)moved_bytes;
    }
    return 0;
}

int main(void)
{
    char log_buf[ADDR_TEXT_MAX_HELPER] = { 0 };
    struct sockaddr_in peer_addr = { 0 };
    sigset_t wait_signal_mask;
    int pipe_fds[2];
    ssize_t moved_bytes;
    int splice_status;
    int exit_status = 1;
    int server_fd = -1;
    int client_fd = -1;

    if (catch_shutdown_signals_helper(&wait_signal_mask) < 0) {
        LOG_ERRNO_HELPER("catch_shutdown_signals_helper()");
        return 1;
    }
    if (ignore_sigpipe_helper() < 0) {
        LOG_ERRNO_HELPER("sigaction(SIGPIPE)");
        return 1;
    }

    if (pipe2(pipe_fds, O_CLOEXEC) < 0) {
        LOG_ERRNO_HELPER("pipe2()");
        return 1;
    }

    server_fd = tcp_listen_helper(PORT_HELPER, BACKLOG_HELPER);
    if (server_fd < 0) {
        LOG_ERRNO_HELPER("tcp_listen_helper()");
        goto cleanup;
    }

    LOG_MSG_HELPER("listening on port %d", PORT_HELPER);

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

        moved_bytes = splice(client_fd, NULL, pipe_fds[PIPE_WRITE_HELPER], NULL, CHUNK_HELPER,
                             SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
        if (moved_bytes < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            LOG_ERRNO_HELPER("splice(socket -> pipe)");
            break;
        } else if (moved_bytes == 0) {
            LOG_MSG_HELPER("%s closed the connection", log_buf);
            break;
        }

        LOG_RESULT_HELPER("relaying %zd bytes from %s", moved_bytes, log_buf);

        splice_status = splice_all_helper(pipe_fds[PIPE_READ_HELPER], client_fd, (size_t)moved_bytes,
                                          &wait_signal_mask);
        if (splice_status < 0) {
            if (should_shut_down_helper()) {
                LOG_MSG_HELPER("stopped by signal %d", which_shutdown_signal_helper());
            } else if (errno == ETIMEDOUT) {
                LOG_MSG_HELPER("%s stopped answering, TCP gave up on the connection", log_buf);
            } else {
                LOG_ERRNO_HELPER("splice(pipe -> socket)");
            }
            break;
        } else if (splice_status > 0) {
            LOG_MSG_HELPER("the pipe ran dry before the relay finished");
            break;
        }
    }

    exit_status = 0;

cleanup:
    if (client_fd >= 0) {
        close(client_fd);
    }
    if (server_fd >= 0) {
        close(server_fd);
    }
    close(pipe_fds[PIPE_READ_HELPER]);
    close(pipe_fds[PIPE_WRITE_HELPER]);
    return exit_status;
}