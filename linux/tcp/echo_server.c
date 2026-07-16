#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

#include "common/net_util.h"

#define PORT 5000
#define BACKLOG 8
#define BUF_SIZE 1024

int main(void)
{
    int listen_fd = tcp_listen(PORT, BACKLOG, 0, 0, NULL, 0);
    if (listen_fd < 0) {
        perror("echo_server: tcp_listen()");
        return 1;
    }

    printf("echo_server: listening on port %d\n", PORT);

    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("echo_server: accept()");
        close(listen_fd);
        return 1;
    }

    char buf[BUF_SIZE];
    while (1) {
        ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
        if (n > 0) {
            if (send_all(client_fd, buf, (size_t)n) < 0) {
                perror("echo_server: send()");
                break;
            }
        } else if (n == 0) {
            break;
        } else {
            if (errno == EINTR) {
                continue;
            }
            perror("echo_server: recv()");
            break;
        }
    }

    close(client_fd);
    close(listen_fd);
    return 0;
}