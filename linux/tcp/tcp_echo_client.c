#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5000
#define BUF_SIZE_HELPER 1024

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char *text_to_send = (argc > 2) ? argv[2] : "hello, socket";
    char log_buf[ADDR_TEXT_MAX_HELPER] = { 0 };
    char recv_buf[BUF_SIZE_HELPER];
    size_t received_total = 0;
    size_t expected_total;
    size_t chunk_len;
    size_t text_len;
    ssize_t received_bytes;
    int fd;

    if (ignore_sigpipe_helper() < 0) {
        LOG_ERRNO_HELPER("sigaction(SIGPIPE)");
        return 1;
    }
    fd = tcp_connect_helper(host, PORT_HELPER, NULL);
    if (fd < 0) {
        if (errno == ETIMEDOUT) {
            LOG_MSG_HELPER("no answer from %s, TCP gave up establishing the connection", host);
        } else {
            LOG_ERRNO_HELPER("tcp_connect_helper()");
        }
        return 1;
    }
    if (format_peer_helper(fd, log_buf, sizeof(log_buf)) < 0) {
        LOG_ERRNO_HELPER("format_peer_helper()");
        close(fd);
        return 1;
    }

    LOG_MSG_HELPER("connected to %s", log_buf);

    text_len = strlen(text_to_send);
    if (send_all_or_shutdown_helper(fd, text_to_send, text_len, NULL) < 0) {
        LOG_ERRNO_HELPER("send()");
        close(fd);
        return 1;
    }

    expected_total = (text_len < sizeof(recv_buf) - 1) ? text_len : sizeof(recv_buf) - 1;
    if (expected_total < text_len) {
        LOG_MSG_HELPER("reading back only %zu of the %zu bytes sent, the buffer holds no more", expected_total,
                       text_len);
    }

    while (received_total < expected_total) {
        chunk_len = expected_total - received_total;
        received_bytes = recv_or_shutdown_helper(fd, recv_buf + received_total, chunk_len, NULL);
        if (received_bytes < 0) {
            if (errno == ETIMEDOUT) {
                LOG_MSG_HELPER("%s stopped answering, TCP gave up on the connection", log_buf);
            } else {
                LOG_ERRNO_HELPER("recv()");
            }

            break;
        } else if (received_bytes == 0) {
            break;
        }
        received_total += (size_t)received_bytes;
    }
    recv_buf[received_total] = '\0';

    LOG_RESULT_HELPER("received '%s' (%zu bytes) from %s", recv_buf, received_total, log_buf);

    close(fd);
    return 0;
}