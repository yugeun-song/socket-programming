#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT_HELPER 5001
#define BUF_SIZE_HELPER 65536

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    char log_buf[ADDR_TEXT_MAX_HELPER] = { 0 };
    unsigned long long received_total = 0;
    char recv_buf[BUF_SIZE_HELPER];
    ssize_t received_bytes;
    int fd;

    if (ignore_sigpipe_helper() < 0) {
        LOG_ERRNO_HELPER("sigaction(SIGPIPE)");
        return 1;
    }
    fd = tcp_connect_helper(host, PORT_HELPER);
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

    while (1) {
        received_bytes = recv_or_shutdown_helper(fd, recv_buf, sizeof(recv_buf), NULL);
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
        received_total += (unsigned long long)received_bytes;
    }

    LOG_RESULT_HELPER("received %llu bytes from %s", received_total, log_buf);

    close(fd);
    return 0;
}