#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5000
#define BACKLOG_HELPER 8
#define CHUNK_HELPER 65536

enum pipe_end {
    PIPE_READ_HELPER,
    PIPE_WRITE_HELPER
};

static int splice_all_helper(int in_fd, int out_fd, size_t len,
                             const sigset_t *const wait_signal_mask_nullable)
{
    while (len > 0) {
        if (wait_ready_or_shutdown_helper(out_fd, POLLOUT, wait_signal_mask_nullable) < 0) {
            return -1;
        }

        ssize_t moved_bytes = splice(in_fd, NULL, out_fd, NULL, len,
                                     SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
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
    sigset_t wait_signal_mask;

    if (catch_shutdown_signals_helper(&wait_signal_mask) < 0) {
        LOG_ERRNO_HELPER("catch_shutdown_signals_helper()");
        return EXIT_FAILURE;
    }
    if (ignore_sigpipe_helper() < 0) {
        LOG_ERRNO_HELPER("sigaction(SIGPIPE)");
        return EXIT_FAILURE;
    }

    int pipe_fds[2];

    if (pipe2(pipe_fds, O_CLOEXEC) < 0) {
        LOG_ERRNO_HELPER("pipe2()");
        return EXIT_FAILURE;
    }

    int exit_status = EXIT_FAILURE;

    int server_fd = tcp_listen_helper(PORT_HELPER, BACKLOG_HELPER);
    if (server_fd < 0) {
        LOG_ERRNO_HELPER("tcp_listen_helper()");
        goto out_close_pipe;
    }

    LOG_MSG_HELPER("listening on port %d", PORT_HELPER);

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

        ssize_t moved_bytes = splice(client_fd, NULL, pipe_fds[PIPE_WRITE_HELPER], NULL,
                                     CHUNK_HELPER, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
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

        int splice_status = splice_all_helper(pipe_fds[PIPE_READ_HELPER], client_fd,
                                              (size_t)moved_bytes, &wait_signal_mask);
        if (splice_status < 0) {
            if (should_shut_down_helper() != 0) {
                LOG_MSG_HELPER("stopped by signal %d", get_shutdown_signal_helper());
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

out_close_client:
    close(client_fd);
out_close_server:
    close(server_fd);
out_close_pipe:
    close(pipe_fds[PIPE_READ_HELPER]);
    close(pipe_fds[PIPE_WRITE_HELPER]);
    return exit_status;
}
