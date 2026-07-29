#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT 5000
#define BUF_SIZE 1024

int main(void)
{
    struct sockaddr_in peer;
    socklen_t peerlen;
    char buf[BUF_SIZE];
    ssize_t n;
    int fd;

    fd = udp_bind(PORT, 0, 0, NULL, 0);
    if (fd < 0) {
        log_errno("udp_bind()");
        return 1;
    }

    log_msg("listening on port %d", PORT);

    while (1) {
        peerlen = sizeof(peer);
        n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&peer, &peerlen);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_errno("recvfrom()");
            break;
        }
        while (1) {
            if (sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&peer, peerlen) >= 0) {
                break;
            }
            if (errno != EINTR) {
                log_errno("sendto()");
                break;
            }
        }
    }

    close(fd);
    return 0;
}