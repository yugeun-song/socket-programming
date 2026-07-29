#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT 5001
#define BUF_SIZE 65536

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    unsigned long long total = 0;
    char buf[BUF_SIZE];
    ssize_t n;
    int fd;

    fd = tcp_connect(host, PORT);
    if (fd < 0) {
        log_errno("tcp_connect()");
        return 1;
    }

    while (1) {
        n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            total += (unsigned long long)n;
        } else if (n == 0) {
            break;
        } else {
            if (errno == EINTR) {
                continue;
            }
            log_errno("recv()");
            break;
        }
    }

    printf("sendfile_client: received %llu bytes\n", total);

    close(fd);
    return 0;
}