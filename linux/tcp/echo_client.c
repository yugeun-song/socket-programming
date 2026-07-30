#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include "common/deadline.h"
#include "common/log.h"
#include "common/net_util.h"

#define PORT 5000
#define BUF_SIZE 1024
#define IDLE_TIMEOUT_MS 5000

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char *message = (argc > 2) ? argv[2] : "hello, socket";
    char peer[PEER_TEXT_MAX] = { 0 };
    struct deadline dl;
    char buf[BUF_SIZE];
    size_t got = 0;
    size_t want;
    ssize_t n;
    int fd;

    if (ignore_sigpipe() < 0) {
        LOG_ERRNO("sigaction(SIGPIPE)");
        return 1;
    }
    if (deadline_start(&dl, IDLE_TIMEOUT_MS) < 0) {
        LOG_ERRNO("clock_gettime()");
        return 1;
    }

    fd = tcp_connect(host, PORT, &dl);
    if (fd < 0) {
        LOG_ERRNO("tcp_connect()");
        return 1;
    }
    if (format_peer(fd, peer, sizeof(peer)) < 0) {
        LOG_ERRNO("format_peer()");
        close(fd);
        return 1;
    }

    LOG_MSG("connected to %s", peer);

    want = strlen(message);
    if (send_all_until(fd, message, want, &dl, NULL) < 0) {
        LOG_ERRNO("send()");
        close(fd);
        return 1;
    }

    while (got < want && got < sizeof(buf) - 1) {
        if (deadline_start(&dl, IDLE_TIMEOUT_MS) < 0) {
            LOG_ERRNO("clock_gettime()");
            break;
        }

        n = recv_until(fd, buf + got, want - got, &dl, NULL);
        if (n < 0) {
            LOG_ERRNO("recv()");
            break;
        }
        if (n == 0) {
            break;
        }
        got += (size_t)n;
    }
    buf[got] = '\0';

    printf("echo_client: received '%s' (%zu bytes) from %s\n", buf, got, peer);

    close(fd);
    return 0;
}