#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT 5000
#define BACKLOG 8
#define CHUNK 65536
#define PIPE_READ 0
#define PIPE_WRITE 1

static int splice_all(int in_fd, int out_fd, size_t len)
{
    ssize_t n;

    while (len > 0) {
        n = splice(in_fd, NULL, out_fd, NULL, len, SPLICE_F_MOVE);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            errno = EPIPE;
            return -1;
        }
        len -= (size_t)n;
    }
    return 0;
}

int main(void)
{
    unsigned long long relayed = 0;
    int pipe_fds[2];
    ssize_t n;
    int listen_fd;
    int client_fd;

    if (ignore_sigpipe() < 0) {
        log_errno("sigaction(SIGPIPE)");
        return 1;
    }

    if (pipe2(pipe_fds, O_CLOEXEC) < 0) {
        log_errno("pipe2()");
        return 1;
    }

    listen_fd = tcp_listen(PORT, BACKLOG, 0, 0, NULL, 0);
    if (listen_fd < 0) {
        log_errno("tcp_listen()");
        close(pipe_fds[PIPE_READ]);
        close(pipe_fds[PIPE_WRITE]);
        return 1;
    }

    log_msg("listening on port %d", PORT);

    client_fd = tcp_accept(listen_fd);
    if (client_fd < 0) {
        log_errno("tcp_accept()");
        close(listen_fd);
        close(pipe_fds[PIPE_READ]);
        close(pipe_fds[PIPE_WRITE]);
        return 1;
    }

    while (1) {
        n = splice(client_fd, NULL, pipe_fds[PIPE_WRITE], NULL, CHUNK, SPLICE_F_MOVE);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_errno("splice(socket -> pipe)");
            break;
        }
        if (n == 0) {
            break;
        }
        if (splice_all(pipe_fds[PIPE_READ], client_fd, (size_t)n) < 0) {
            log_errno("splice(pipe -> socket)");
            break;
        }
        relayed += (unsigned long long)n;
    }

    printf("splice_echo_server: relayed %llu bytes\n", relayed);

    close(client_fd);
    close(listen_fd);
    close(pipe_fds[PIPE_READ]);
    close(pipe_fds[PIPE_WRITE]);
    return 0;
}