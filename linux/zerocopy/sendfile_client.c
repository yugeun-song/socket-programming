#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include "common/deadline.h"
#include "common/log.h"
#include "common/net_util.h"

#define PORT 5001
#define BUF_SIZE 65536
#define IDLE_TIMEOUT_MS 5000

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    char peer[PEER_TEXT_MAX] = { 0 };
    unsigned long long total = 0;
    struct deadline dl;
    char buf[BUF_SIZE];
    ssize_t n;
    int fd;

    if (ignore_sigpipe() < 0) {
        log_errno("sigaction(SIGPIPE)");
        return 1;
    }
    if (deadline_start(&dl, IDLE_TIMEOUT_MS) < 0) {
        log_errno("clock_gettime()");
        return 1;
    }

    fd = tcp_connect(host, PORT, &dl);
    if (fd < 0) {
        log_errno("tcp_connect()");
        return 1;
    }
    if (format_peer(fd, peer, sizeof(peer)) < 0) {
        log_errno("format_peer()");
        close(fd);
        return 1;
    }

    log_msg("connected to %s", peer);

    while (1) {
        if (deadline_start(&dl, IDLE_TIMEOUT_MS) < 0) {
            log_errno("clock_gettime()");
            break;
        }

        n = recv_until(fd, buf, sizeof(buf), &dl, NULL);
        if (n < 0) {
            log_errno("recv()");
            break;
        }
        if (n == 0) {
            break;
        }
        total += (unsigned long long)n;
    }

    printf("sendfile_client: received %llu bytes from %s\n", total, peer);

    close(fd);
    return 0;
}