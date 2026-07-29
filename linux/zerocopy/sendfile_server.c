#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "common/log.h"
#include "common/net_util.h"

#define PORT 5001
#define BACKLOG 8

int main(int argc, char **argv)
{
    struct stat st;
    off_t offset = 0;
    ssize_t n;
    int file_fd;
    int listen_fd;
    int client_fd;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 1;
    }

    if (ignore_sigpipe() < 0) {
        log_errno("sigaction(SIGPIPE)");
        return 1;
    }

    file_fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (file_fd < 0) {
        log_errno("open()");
        return 1;
    }

    if (fstat(file_fd, &st) < 0) {
        log_errno("fstat()");
        close(file_fd);
        return 1;
    }

    listen_fd = tcp_listen(PORT, BACKLOG, 0, 0, NULL, 0);
    if (listen_fd < 0) {
        log_errno("tcp_listen()");
        close(file_fd);
        return 1;
    }

    log_msg("serving %s (%lld bytes) on port %d", argv[1], (long long)st.st_size, PORT);

    client_fd = tcp_accept(listen_fd);
    if (client_fd < 0) {
        log_errno("tcp_accept()");
        close(listen_fd);
        close(file_fd);
        return 1;
    }

    while (offset < st.st_size) {
        n = sendfile(client_fd, file_fd, &offset, (size_t)(st.st_size - offset));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_errno("sendfile()");
            break;
        }
        if (n == 0) {
            break;
        }
    }

    printf("sendfile_server: sent %lld bytes\n", (long long)offset);

    close(client_fd);
    close(listen_fd);
    close(file_fd);
    return 0;
}