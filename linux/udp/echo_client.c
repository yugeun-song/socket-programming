#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include "common/deadline.h"
#include "common/log.h"
#include "common/net_util.h"

#define PORT 5000
#define BUF_SIZE 1024
#define IDLE_TIMEOUT_MS 5000

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char *message = (argc > 2) ? argv[2] : "hello, udp";
    char peer[PEER_TEXT_MAX] = { 0 };
    struct sockaddr_in from = { 0 };
    struct deadline dl;
    char buf[BUF_SIZE];
    ssize_t n;
    int fd;

    fd = udp_connect(host, PORT);
    if (fd < 0) {
        LOG_ERRNO("udp_connect()");
        return 1;
    }

    if (deadline_start(&dl, IDLE_TIMEOUT_MS) < 0) {
        LOG_ERRNO("clock_gettime()");
        close(fd);
        return 1;
    }

    if (send_all_until(fd, message, strlen(message), &dl, NULL) < 0) {
        LOG_ERRNO("send()");
        close(fd);
        return 1;
    }

    n = recvfrom_until(fd, buf, sizeof(buf) - 1, &from, &dl, NULL);
    if (n < 0) {
        LOG_ERRNO("recvfrom()");
        close(fd);
        return 1;
    }
    buf[n] = '\0';

    if (format_addr(&from, peer, sizeof(peer)) < 0) {
        LOG_ERRNO("format_addr()");
        close(fd);
        return 1;
    }

    printf("udp echo_client: received '%s' (%zd bytes) from %s\n", buf, n, peer);

    close(fd);
    return 0;
}