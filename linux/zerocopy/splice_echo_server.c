#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include "common/deadline.h"
#include "common/log.h"
#include "common/net_util.h"

#define PORT 5000
#define BACKLOG 8
#define CHUNK 65536
#define PIPE_READ 0
#define PIPE_WRITE 1
#define IDLE_TIMEOUT_MS 5000

static int splice_all(int in_fd, int out_fd, size_t len, const struct deadline *dl, const sigset_t *mask)
{
    ssize_t n;

    while (len > 0) {
        if (wait_ready(out_fd, POLLOUT, dl, mask) < 0) {
            return -1;
        }

        n = splice(in_fd, NULL, out_fd, NULL, len, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
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
    char peer[PEER_TEXT_MAX] = { 0 };
    struct sockaddr_in addr = { 0 };
    unsigned long long relayed = 0;
    struct deadline accept_dl;
    struct deadline io_dl;
    sigset_t saved_mask;
    int pipe_fds[2];
    ssize_t n;
    int listen_fd;
    int client_fd;

    if (install_stop_handlers(&saved_mask) < 0) {
        LOG_ERRNO("install_stop_handlers()");
        return 1;
    }
    if (ignore_sigpipe() < 0) {
        LOG_ERRNO("sigaction(SIGPIPE)");
        return 1;
    }

    if (pipe2(pipe_fds, O_CLOEXEC) < 0) {
        LOG_ERRNO("pipe2()");
        return 1;
    }

    listen_fd = tcp_listen(PORT, BACKLOG, 0, 0, NULL, 0);
    if (listen_fd < 0) {
        LOG_ERRNO("tcp_listen()");
        close(pipe_fds[PIPE_READ]);
        close(pipe_fds[PIPE_WRITE]);
        return 1;
    }

    LOG_MSG("listening on port %d", PORT);

    if (deadline_start(&accept_dl, DEADLINE_FOREVER) < 0) {
        LOG_ERRNO("clock_gettime()");
        close(listen_fd);
        close(pipe_fds[PIPE_READ]);
        close(pipe_fds[PIPE_WRITE]);
        return 1;
    }

    client_fd = tcp_accept(listen_fd, &addr, &accept_dl, &saved_mask);
    if (client_fd < 0) {
        if (errno == ECANCELED) {
            LOG_MSG("stopped by signal %d", stop_signal());
            close(listen_fd);
            close(pipe_fds[PIPE_READ]);
            close(pipe_fds[PIPE_WRITE]);
            return 0;
        }
        LOG_ERRNO("tcp_accept()");
        close(listen_fd);
        close(pipe_fds[PIPE_READ]);
        close(pipe_fds[PIPE_WRITE]);
        return 1;
    }
    if (format_addr(&addr, peer, sizeof(peer)) < 0) {
        LOG_ERRNO("format_addr()");
        close(client_fd);
        close(listen_fd);
        close(pipe_fds[PIPE_READ]);
        close(pipe_fds[PIPE_WRITE]);
        return 1;
    }

    LOG_MSG("accepted %s", peer);

    while (1) {
        if (deadline_start(&io_dl, IDLE_TIMEOUT_MS) < 0) {
            LOG_ERRNO("clock_gettime()");
            break;
        }
        if (wait_ready(client_fd, POLLIN, &io_dl, &saved_mask) < 0) {
            if (errno == ECANCELED) {
                LOG_MSG("stopped by signal %d", stop_signal());
                break;
            }
            LOG_ERRNO("wait_ready()");
            break;
        }

        n = splice(client_fd, NULL, pipe_fds[PIPE_WRITE], NULL, CHUNK, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            LOG_ERRNO("splice(socket -> pipe)");
            break;
        }
        if (n == 0) {
            LOG_MSG("%s closed the connection", peer);
            break;
        }

        printf("splice_echo_server: relaying %zd bytes from %s\n", n, peer);
        fflush(stdout);

        if (splice_all(pipe_fds[PIPE_READ], client_fd, (size_t)n, &io_dl, &saved_mask) < 0) {
            if (errno == ECANCELED) {
                LOG_MSG("stopped by signal %d", stop_signal());
                break;
            }
            LOG_ERRNO("splice(pipe -> socket)");
            break;
        }
        relayed += (unsigned long long)n;
    }

    printf("splice_echo_server: relayed %llu bytes to %s\n", relayed, peer);

    close(client_fd);
    close(listen_fd);
    close(pipe_fds[PIPE_READ]);
    close(pipe_fds[PIPE_WRITE]);
    return 0;
}