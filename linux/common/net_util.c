#define _GNU_SOURCE

#include "common/net_util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static volatile sig_atomic_t gs_shutdown_signal_number;

static void handle_shutdown_signal_helper(int signal_number)
{
    gs_shutdown_signal_number = signal_number;
}

static int bind_inet_helper(int sock_type, unsigned short port)
{
    int fd = socket(AF_INET, sock_type | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return -1;
    }

    int is_reuse_enabled = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &is_reuse_enabled, sizeof(is_reuse_enabled)) < 0) {
        goto out_close_fd;
    }

    struct sockaddr_in local_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        goto out_close_fd;
    }

    return fd;

out_close_fd:
    close_keeping_errno_helper(fd);
    return -1;
}

static int wait_connected_helper(int fd, const sigset_t *const wait_signal_mask_nullable)
{
    if (wait_ready_or_shutdown_helper(fd, POLLOUT, wait_signal_mask_nullable) < 0) {
        return -1;
    }

    int sock_error = 0;
    socklen_t sock_error_len = sizeof(sock_error);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_error, &sock_error_len) < 0) {
        return -1;
    }
    if (sock_error != 0) {
        errno = sock_error;
        return -1;
    }
    return 0;
}

static int connect_inet_helper(int sock_type, const char *const host, unsigned short port,
                               const sigset_t *const wait_signal_mask_nullable)
{
    int fd = socket(AF_INET, sock_type | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_in peer_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port)
    };

    if (inet_pton(AF_INET, host, &peer_addr.sin_addr) != 1) {
        errno = EINVAL;
        goto out_close_fd;
    }

    if (connect(fd, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
        if (errno != EINPROGRESS && errno != EINTR) {
            goto out_close_fd;
        }
        if (wait_connected_helper(fd, wait_signal_mask_nullable) < 0) {
            goto out_close_fd;
        }
    }

    return fd;

out_close_fd:
    close_keeping_errno_helper(fd);
    return -1;
}

void close_keeping_errno_helper(int fd)
{
    int saved_errno = errno;

    close(fd);
    errno = saved_errno;
}

int tcp_listen_helper(unsigned short port, int backlog)
{
    int fd = bind_inet_helper(SOCK_STREAM, port);
    if (fd < 0) {
        return -1;
    }

    if (listen(fd, backlog) < 0) {
        goto out_close_fd;
    }

    return fd;

out_close_fd:
    close_keeping_errno_helper(fd);
    return -1;
}

int tcp_connect_helper(const char *const host, unsigned short port,
                       const sigset_t *const wait_signal_mask_nullable)
{
    return connect_inet_helper(SOCK_STREAM, host, port, wait_signal_mask_nullable);
}

int tcp_accept_helper(int server_fd, struct sockaddr_in *const mut_peer_addr,
                      const sigset_t *const wait_signal_mask_nullable)
{
    socklen_t peer_addr_len;
    int fd;

    for (;;) {
        if (wait_ready_or_shutdown_helper(server_fd, POLLIN, wait_signal_mask_nullable) < 0) {
            return -1;
        }

        peer_addr_len = sizeof(*mut_peer_addr);
        fd = accept4(server_fd, (struct sockaddr *)mut_peer_addr, &peer_addr_len,
                     SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd >= 0) {
            break;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED) {
            return -1;
        }
    }

    if (peer_addr_len != sizeof(*mut_peer_addr) || mut_peer_addr->sin_family != AF_INET) {
        errno = EAFNOSUPPORT;
        goto out_close_fd;
    }

    return fd;

out_close_fd:
    close_keeping_errno_helper(fd);
    return -1;
}

int udp_bind_helper(unsigned short port)
{
    return bind_inet_helper(SOCK_DGRAM, port);
}

int udp_connect_helper(const char *const host, unsigned short port)
{
    return connect_inet_helper(SOCK_DGRAM, host, port, NULL);
}

int wait_ready_or_shutdown_helper(int fd, short events,
                                  const sigset_t *const wait_signal_mask_nullable)
{
    struct pollfd poll_target = {
        .fd = fd,
        .events = events
    };

    for (;;) {
        if (should_shut_down_helper() != 0) {
            return -1;
        }

        int poll_status = ppoll(&poll_target, 1, NULL, wait_signal_mask_nullable);
        if (poll_status > 0) {
            return poll_status;
        }
        if (poll_status < 0 && errno != EINTR) {
            return -1;
        }
    }
}

ssize_t recv_or_shutdown_helper(int fd, void *const mut_recv_buf, size_t len,
                                const sigset_t *const wait_signal_mask_nullable)
{
    for (;;) {
        if (wait_ready_or_shutdown_helper(fd, POLLIN, wait_signal_mask_nullable) < 0) {
            return -1;
        }

        ssize_t received_bytes = recv(fd, mut_recv_buf, len, 0);
        if (received_bytes >= 0) {
            return received_bytes;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
    }
}

ssize_t recvfrom_or_shutdown_helper(int fd, void *const mut_recv_buf, size_t len,
                                    struct sockaddr_in *const mut_peer_addr,
                                    const sigset_t *const wait_signal_mask_nullable)
{
    for (;;) {
        if (wait_ready_or_shutdown_helper(fd, POLLIN, wait_signal_mask_nullable) < 0) {
            return -1;
        }

        socklen_t peer_addr_len = sizeof(*mut_peer_addr);
        ssize_t received_bytes = recvfrom(fd, mut_recv_buf, len, 0,
                                          (struct sockaddr *)mut_peer_addr, &peer_addr_len);
        if (received_bytes >= 0) {
            if (peer_addr_len != sizeof(*mut_peer_addr) || mut_peer_addr->sin_family != AF_INET) {
                errno = EAFNOSUPPORT;
                return -1;
            }
            return received_bytes;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
    }
}

int send_all_or_shutdown_helper(int fd, const void *const send_buf, size_t len,
                                const sigset_t *const wait_signal_mask_nullable)
{
    const char *const data = send_buf;
    size_t sent_total = 0;

    while (sent_total < len) {
        if (wait_ready_or_shutdown_helper(fd, POLLOUT, wait_signal_mask_nullable) < 0) {
            return -1;
        }

        ssize_t sent_bytes = send(fd, data + sent_total, len - sent_total, MSG_NOSIGNAL);
        if (sent_bytes < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        sent_total += (size_t)sent_bytes;
    }
    return 0;
}

int format_addr_helper(char *const mut_out, size_t len, const struct sockaddr_in *const addr)
{
    char host[INET_ADDRSTRLEN] = { 0 };

    if (inet_ntop(AF_INET, &addr->sin_addr, host, sizeof(host)) == NULL) {
        return -1;
    }

    int written = snprintf(mut_out, len, "%s:%u", host, ntohs(addr->sin_port));
    if (written < 0) {
        return -1;
    }
    if ((size_t)written >= len) {
        errno = ENOSPC;
        return -1;
    }
    return 0;
}

int format_peer_helper(int fd, char *const mut_out, size_t len)
{
    struct sockaddr_in peer_addr = { 0 };
    socklen_t peer_addr_len = sizeof(peer_addr);

    if (getpeername(fd, (struct sockaddr *)&peer_addr, &peer_addr_len) < 0) {
        return -1;
    }
    if (peer_addr_len != sizeof(peer_addr) || peer_addr.sin_family != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    return format_addr_helper(mut_out, len, &peer_addr);
}

int register_signal_handler_helper(int signal_number, void (*const handler)(int), int flags)
{
    struct sigaction sa = {
        .sa_handler = handler,
        .sa_flags = flags
    };

    if (sigemptyset(&sa.sa_mask) < 0) {
        return -1;
    }
    return sigaction(signal_number, &sa, NULL);
}

int ignore_sigpipe_helper(void)
{
    return register_signal_handler_helper(SIGPIPE, SIG_IGN, 0);
}

int catch_shutdown_signals_helper(sigset_t *const mut_wait_signal_mask)
{
    static const int s_shutdown_signals[] = { SIGINT, SIGTERM };
    sigset_t signals_to_block;

    if (sigemptyset(&signals_to_block) < 0) {
        return -1;
    }

    for (size_t i = 0; i < ARRAY_SIZE(s_shutdown_signals); ++i) {
        int signal_number = s_shutdown_signals[i];

        if (register_signal_handler_helper(signal_number, handle_shutdown_signal_helper, 0) < 0) {
            return -1;
        }
        if (sigaddset(&signals_to_block, signal_number) < 0) {
            return -1;
        }
    }

    int mask_error_code = pthread_sigmask(SIG_BLOCK, &signals_to_block, mut_wait_signal_mask);
    if (mask_error_code != 0) {
        errno = mask_error_code;
        return -1;
    }
    return 0;
}

int should_shut_down_helper(void)
{
    return gs_shutdown_signal_number != 0;
}

int get_shutdown_signal_helper(void)
{
    return (int)gs_shutdown_signal_number;
}
