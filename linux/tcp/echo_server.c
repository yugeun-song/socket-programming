#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT 5000
#define BACKLOG 8
#define BUF_SIZE 1024

int main(void)
{
    int listen_fd = tcp_listen(PORT, BACKLOG, 0, 0, NULL, 0);
    if (listen_fd < 0) {
        log_errno("tcp_listen()");
        return 1;
    }

    log_msg("listening on port %d", PORT);

    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        log_errno("accept()");
        close(listen_fd);
        return 1;
    }

    char buf[BUF_SIZE];
    while (1) {
        ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
        if (n > 0) {
            if (send_all(client_fd, buf, (size_t)n) < 0) {
                log_errno("send()");
                break;
            }
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

    close(client_fd);
    close(listen_fd);
    return 0;
}
