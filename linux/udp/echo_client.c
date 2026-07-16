#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "common/net_util.h"

#define PORT 5000
#define BUF_SIZE 1024

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char *message = (argc > 2) ? argv[2] : "hello, udp";

    int fd = udp_connect(host, PORT);
    if (fd < 0) {
        perror("udp echo_client: udp_connect()");
        return 1;
    }

    if (send(fd, message, strlen(message), 0) < 0) {
        perror("udp echo_client: send()");
        close(fd);
        return 1;
    }

    char buf[BUF_SIZE];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n < 0) {
        perror("udp echo_client: recv()");
        close(fd);
        return 1;
    }
    buf[n] = '\0';
    printf("udp echo_client: received '%s'\n", buf);

    close(fd);
    return 0;
}