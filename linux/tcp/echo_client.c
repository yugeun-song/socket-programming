#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT 5000
#define BUF_SIZE 1024

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char *message = (argc > 2) ? argv[2] : "hello, socket";

    int fd = tcp_connect(host, PORT);
    if (fd < 0) {
        log_errno("tcp_connect()");
        return 1;
    }

    size_t want = strlen(message);
    if (send_all(fd, message, want) < 0) {
        log_errno("send()");
        close(fd);
        return 1;
    }

    char buf[BUF_SIZE];
    size_t got = 0;
    while (got < want && got < sizeof(buf) - 1) {
        ssize_t n = recv(fd, buf + got, want - got, 0);
        if (n > 0) {
            got += (size_t)n;
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
    buf[got] = '\0';
    log_msg("received '%s'", buf);

    close(fd);
    return 0;
}
