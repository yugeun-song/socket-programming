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

#include "common/deadline.h"
#include "common/log.h"
#include "common/net_util.h"

#define PORT 5001
#define BACKLOG 8
#define IDLE_TIMEOUT_MS 5000

int main(int argc, char **argv)
{
    char peer[PEER_TEXT_MAX] = { 0 };
    struct sockaddr_in addr = { 0 };
    struct deadline accept_dl;
    struct deadline io_dl;
    sigset_t saved_mask;
    struct stat st;
    off_t offset = 0;
    ssize_t n;
    int file_fd;
    int listen_fd;
    int client_fd;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 1;
    }

    if (install_stop_handlers(&saved_mask) < 0) {
        LOG_ERRNO("install_stop_handlers()");
        return 1;
    }
    if (ignore_sigpipe() < 0) {
        LOG_ERRNO("sigaction(SIGPIPE)");
        return 1;
    }

    file_fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (file_fd < 0) {
        LOG_ERRNO("open()");
        return 1;
    }

    if (fstat(file_fd, &st) < 0) {
        LOG_ERRNO("fstat()");
        close(file_fd);
        return 1;
    }

    listen_fd = tcp_listen(PORT, BACKLOG, 0, 0, NULL, 0);
    if (listen_fd < 0) {
        LOG_ERRNO("tcp_listen()");
        close(file_fd);
        return 1;
    }

    LOG_MSG("serving %s (%lld bytes) on port %d", argv[1], (long long)st.st_size, PORT);

    if (deadline_start(&accept_dl, DEADLINE_FOREVER) < 0) {
        LOG_ERRNO("clock_gettime()");
        close(listen_fd);
        close(file_fd);
        return 1;
    }

    client_fd = tcp_accept(listen_fd, &addr, &accept_dl, &saved_mask);
    if (client_fd < 0) {
        LOG_ERRNO("tcp_accept()");
        close(listen_fd);
        close(file_fd);
        return 1;
    }
    if (format_addr(&addr, peer, sizeof(peer)) < 0) {
        LOG_ERRNO("format_addr()");
        close(client_fd);
        close(listen_fd);
        close(file_fd);
        return 1;
    }

    LOG_MSG("accepted %s", peer);

    while (offset < st.st_size) {
        if (deadline_start(&io_dl, IDLE_TIMEOUT_MS) < 0) {
            LOG_ERRNO("clock_gettime()");
            break;
        }
        if (wait_ready(client_fd, POLLOUT, &io_dl, &saved_mask) < 0) {
            LOG_ERRNO("wait_ready()");
            break;
        }

        n = sendfile(client_fd, file_fd, &offset, (size_t)(st.st_size - offset));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            LOG_ERRNO("sendfile()");
            break;
        }
        if (n == 0) {
            break;
        }
    }

    printf("sendfile_server: sent %lld bytes to %s\n", (long long)offset, peer);

    close(client_fd);
    close(listen_fd);
    close(file_fd);
    return 0;
}