#define _POSIX_C_SOURCE 200809L

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5000
#define BUF_SIZE_HELPER 1024

int main(int argc, char *argv[])
{
    const char *const host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char *const text_to_send = (argc > 2) ? argv[2] : "hello, udp";

    int fd = udp_connect_helper(host, PORT_HELPER);
    if (fd < 0) {
        LOG_ERRNO_HELPER("udp_connect_helper()");
        return EXIT_FAILURE;
    }

    int exit_status = EXIT_FAILURE;

    if (send_all_or_shutdown_helper(fd, text_to_send, strlen(text_to_send), NULL) < 0) {
        LOG_ERRNO_HELPER("send()");
        goto out_close_fd;
    }

    char recv_buf[BUF_SIZE_HELPER];
    struct sockaddr_in peer_addr = { 0 };

    ssize_t received_bytes = recvfrom_or_shutdown_helper(fd, recv_buf, sizeof(recv_buf) - 1,
                                                         &peer_addr, NULL);
    if (received_bytes < 0) {
        LOG_ERRNO_HELPER("recvfrom()");
        goto out_close_fd;
    }
    recv_buf[received_bytes] = '\0';

    char log_buf[ADDR_TEXT_MAX_HELPER] = { 0 };

    if (format_addr_helper(log_buf, sizeof(log_buf), &peer_addr) < 0) {
        LOG_ERRNO_HELPER("format_addr_helper()");
        goto out_close_fd;
    }

    LOG_RESULT_HELPER("received '%s' (%zd bytes) from %s", recv_buf, received_bytes, log_buf);
    exit_status = 0;

out_close_fd:
    close(fd);
    return exit_status;
}
