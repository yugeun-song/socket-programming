#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT 5000
#define BUF_SIZE 1024

int main(void)
{
    int fd = udp_bind(PORT, 0, 0, NULL, 0);
    if (fd < 0) {
        log_errno("udp_bind()");
        return 1;
    }

    log_msg("listening on port %d", PORT);

    char buf[BUF_SIZE];
    while (1) {
        struct sockaddr_in peer;
        socklen_t peerlen = sizeof(peer);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&peer, &peerlen);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_errno("recvfrom()");
            break;
        }
        if (sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&peer, peerlen) < 0) {
            log_errno("sendto()");
        }
    }

    close(fd);
    return 0;
}
