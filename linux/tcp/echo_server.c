#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include "common/deadline.h"
#include "common/log.h"
#include "common/net_util.h"

#define PORT 5000
#define BACKLOG 8
#define BUF_SIZE 1024
#define IDLE_TIMEOUT_MS 5000

int main(void)
{
    char peer[PEER_TEXT_MAX] = { 0 };
    struct sockaddr_in addr = { 0 };
    struct deadline accept_dl;
    struct deadline io_dl;
    sigset_t saved_mask;
    char buf[BUF_SIZE];
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

    listen_fd = tcp_listen(PORT, BACKLOG, 0, 0, NULL, 0);
    if (listen_fd < 0) {
        LOG_ERRNO("tcp_listen()");
        return 1;
    }

    LOG_MSG("listening on port %d", PORT);

    if (deadline_start(&accept_dl, DEADLINE_FOREVER) < 0) {
        LOG_ERRNO("clock_gettime()");
        close(listen_fd);
        return 1;
    }

    client_fd = tcp_accept(listen_fd, &addr, &accept_dl, &saved_mask);
    if (client_fd < 0) {
        if (errno == ECANCELED) {
            LOG_MSG("stopped by signal %d", stop_signal());
            close(listen_fd);
            return 0;
        }
        LOG_ERRNO("tcp_accept()");
        close(listen_fd);
        return 1;
    }
    if (format_addr(&addr, peer, sizeof(peer)) < 0) {
        LOG_ERRNO("format_addr()");
        close(client_fd);
        close(listen_fd);
        return 1;
    }

    LOG_MSG("accepted %s", peer);

    while (1) {
        if (deadline_start(&io_dl, IDLE_TIMEOUT_MS) < 0) {
            LOG_ERRNO("clock_gettime()");
            break;
        }

        n = recv_until(client_fd, buf, sizeof(buf), &io_dl, &saved_mask);
        if (n < 0) {
            if (errno == ECANCELED) {
                LOG_MSG("stopped by signal %d", stop_signal());
                break;
            }
            LOG_ERRNO("recv()");
            break;
        }
        if (n == 0) {
            LOG_MSG("%s closed the connection", peer);
            break;
        }

        printf("echo_server: received %zd bytes from %s\n", n, peer);
        fflush(stdout);

        if (send_all_until(client_fd, buf, (size_t)n, &io_dl, &saved_mask) < 0) {
            if (errno == ECANCELED) {
                LOG_MSG("stopped by signal %d", stop_signal());
                break;
            }
            LOG_ERRNO("send()");
            break;
        }
    }

    close(client_fd);
    close(listen_fd);
    return 0;
}