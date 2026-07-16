#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "common/net_util.h"

#define PORT 5000
#define BUF_SIZE 1024

int main(void)
{
    int fd = udp_bind(PORT, 0, 0, NULL, 0);
    if (fd < 0) {
        perror("udp echo_server: udp_bind()");
        return 1;
    }

    printf("udp echo_server: listening on port %d\n", PORT);

    char buf[BUF_SIZE];
    while (1) {
        struct sockaddr_in peer;
        socklen_t peerlen = sizeof(peer);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&peer, &peerlen);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("udp echo_server: recvfrom()");
            break;
        }
        if (sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&peer, peerlen) < 0) {
            perror("udp echo_server: sendto()");
        }
    }

    close(fd);
    return 0;
}