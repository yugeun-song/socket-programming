#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include "common/deadline.h"
#include "common/log.h"
#include "common/net_util.h"

#define PORT 5000
#define BUF_SIZE 1024
#define IDLE_TIMEOUT_MS 5000

static int send_reply(int fd, const void *buf, size_t len, const struct sockaddr_in *peer, const struct deadline *dl,
                      const sigset_t *mask)
{
    while (1) {
        if (wait_ready(fd, POLLOUT, dl, mask) < 0) {
            return -1;
        }
        if (sendto(fd, buf, len, 0, (const struct sockaddr *)peer, sizeof(*peer)) >= 0) {
            return 0;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
    }
}

int main(void)
{
    char peer[PEER_TEXT_MAX] = { 0 };
    struct sockaddr_in addr = { 0 };
    struct deadline recv_dl;
    struct deadline send_dl;
    sigset_t saved_mask;
    char buf[BUF_SIZE];
    ssize_t n;
    int fd;

    if (install_stop_handlers(&saved_mask) < 0) {
        LOG_ERRNO("install_stop_handlers()");
        return 1;
    }

    fd = udp_bind(PORT, 0, 0, NULL, 0);
    if (fd < 0) {
        LOG_ERRNO("udp_bind()");
        return 1;
    }

    LOG_MSG("listening on port %d", PORT);

    if (deadline_start(&recv_dl, DEADLINE_FOREVER) < 0) {
        LOG_ERRNO("clock_gettime()");
        close(fd);
        return 1;
    }

    while (1) {
        n = recvfrom_until(fd, buf, sizeof(buf), &addr, &recv_dl, &saved_mask);
        if (n < 0) {
            if (errno == ECANCELED) {
                LOG_MSG("stopped by signal %d", stop_signal());
                break;
            }
            LOG_ERRNO("recvfrom()");
            break;
        }
        if (format_addr(&addr, peer, sizeof(peer)) < 0) {
            LOG_ERRNO("format_addr()");
            break;
        }

        printf("udp echo_server: received %zd bytes from %s\n", n, peer);
        fflush(stdout);

        if (deadline_start(&send_dl, IDLE_TIMEOUT_MS) < 0) {
            LOG_ERRNO("clock_gettime()");
            break;
        }
        if (send_reply(fd, buf, (size_t)n, &addr, &send_dl, &saved_mask) < 0) {
            if (errno == ECANCELED) {
                LOG_MSG("stopped by signal %d", stop_signal());
                break;
            }
            LOG_ERRNO("sendto()");
        }
    }

    close(fd);
    return 0;
}