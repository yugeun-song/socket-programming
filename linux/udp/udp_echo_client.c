#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5000
#define BUF_SIZE_HELPER 1024

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char *text_to_send = (argc > 2) ? argv[2] : "hello, udp";
    char log_buf[ADDR_TEXT_MAX_HELPER] = { 0 };
    struct sockaddr_in peer_addr = { 0 };
    char recv_buf[BUF_SIZE_HELPER];
    ssize_t received_bytes;
    int fd;

    fd = udp_connect_helper(host, PORT_HELPER);
    if (fd < 0) {
        LOG_ERRNO_HELPER("udp_connect_helper()");
        return 1;
    }

    if (send_all_or_shutdown_helper(fd, text_to_send, strlen(text_to_send), NULL) < 0) {
        LOG_ERRNO_HELPER("send()");
        close(fd);
        return 1;
    }

    received_bytes = recvfrom_or_shutdown_helper(fd, recv_buf, sizeof(recv_buf) - 1, &peer_addr, NULL);
    if (received_bytes < 0) {
        LOG_ERRNO_HELPER("recvfrom()");
        close(fd);
        return 1;
    }
    recv_buf[received_bytes] = '\0';

    if (format_addr_helper(&peer_addr, log_buf, sizeof(log_buf)) < 0) {
        LOG_ERRNO_HELPER("format_addr_helper()");
        close(fd);
        return 1;
    }

    LOG_RESULT_HELPER("received '%s' (%zd bytes) from %s", recv_buf, received_bytes, log_buf);

    close(fd);
    return 0;
}