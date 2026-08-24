#define _GNU_SOURCE

#include "common/net_util.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

static volatile sig_atomic_t g_shutdown_signal_number;

static void on_shutdown_signal_helper(int signal_number)
{
    g_shutdown_signal_number = signal_number;
}

static int bind_inet_helper(int sock_type, unsigned short port)
{
    struct sockaddr_in local_addr = { 0 };
    int reuse_enable = 1;
    int saved_errno;
    int fd;

    fd = socket(AF_INET, sock_type | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_enable, sizeof(reuse_enable)) < 0) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    return fd;
}

static int wait_connected_helper(int fd)
{
    socklen_t sock_error_len = sizeof(int);
    int sock_error = 0;

    if (wait_ready_or_shutdown_helper(fd, POLLOUT, NULL) < 0) {
        return -1;
    }
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_error, &sock_error_len) < 0) {
        return -1;
    }
    if (sock_error != 0) {
        errno = sock_error;
        return -1;
    }
    return 0;
}

static int connect_inet_helper(int sock_type, const char *host, unsigned short port)
{
    struct sockaddr_in peer_addr = { 0 };
    int saved_errno;
    int fd;

    fd = socket(AF_INET, sock_type | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return -1;
    }

    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &peer_addr.sin_addr) != 1) {
        close(fd);
        errno = EINVAL;
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
        if (errno != EINPROGRESS && errno != EINTR) {
            saved_errno = errno;
            close(fd);
            errno = saved_errno;
            return -1;
        }
        if (wait_connected_helper(fd) < 0) {
            saved_errno = errno;
            close(fd);
            errno = saved_errno;
            return -1;
        }
    }

    return fd;
}

int tcp_listen_helper(unsigned short port, int backlog)
{
    int saved_errno;
    int fd;

    fd = bind_inet_helper(SOCK_STREAM, port);
    if (fd < 0) {
        return -1;
    }

    if (listen(fd, backlog) < 0) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    return fd;
}

int tcp_connect_helper(const char *host, unsigned short port)
{
    return connect_inet_helper(SOCK_STREAM, host, port);
}

int tcp_accept_helper(int server_fd, struct sockaddr_in *peer_addr, const sigset_t *wait_signal_mask)
{
    socklen_t peer_addr_len;
    int fd;

    while (1) {
        if (wait_ready_or_shutdown_helper(server_fd, POLLIN, wait_signal_mask) < 0) {
            return -1;
        }

        peer_addr_len = sizeof(*peer_addr);
        fd = accept4(server_fd, (struct sockaddr *)peer_addr, &peer_addr_len, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd >= 0) {
            break;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED) {
            return -1;
        }
    }

    if (peer_addr_len != sizeof(*peer_addr) || peer_addr->sin_family != AF_INET) {
        close(fd);
        errno = EAFNOSUPPORT;
        return -1;
    }

    return fd;
}

int udp_bind_helper(unsigned short port)
{
    return bind_inet_helper(SOCK_DGRAM, port);
}

int udp_connect_helper(const char *host, unsigned short port)
{
    return connect_inet_helper(SOCK_DGRAM, host, port);
}

int wait_ready_or_shutdown_helper(int fd, short events, const sigset_t *wait_signal_mask)
{
    struct pollfd poll_target = { 0 };
    int poll_status;

    poll_target.fd = fd;
    poll_target.events = events;

    while (1) {
        if (should_shut_down_helper()) {
            return -1;
        }

        poll_status = ppoll(&poll_target, 1, NULL, wait_signal_mask);
        if (poll_status > 0) {
            return poll_status;
        }
        if (poll_status < 0 && errno != EINTR) {
            return -1;
        }
    }
}

ssize_t recv_or_shutdown_helper(int fd, void *recv_buf, size_t len, const sigset_t *wait_signal_mask)
{
    ssize_t received_bytes;

    while (1) {
        if (wait_ready_or_shutdown_helper(fd, POLLIN, wait_signal_mask) < 0) {
            return -1;
        }

        received_bytes = recv(fd, recv_buf, len, 0);
        if (received_bytes >= 0) {
            return received_bytes;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
    }
}

ssize_t recvfrom_or_shutdown_helper(int fd, void *recv_buf, size_t len, struct sockaddr_in *peer_addr,
                                    const sigset_t *wait_signal_mask)
{
    ssize_t received_bytes;
    socklen_t peer_addr_len;

    while (1) {
        if (wait_ready_or_shutdown_helper(fd, POLLIN, wait_signal_mask) < 0) {
            return -1;
        }

        peer_addr_len = sizeof(*peer_addr);
        received_bytes = recvfrom(fd, recv_buf, len, 0, (struct sockaddr *)peer_addr, &peer_addr_len);
        if (received_bytes >= 0) {
            if (peer_addr_len != sizeof(*peer_addr) || peer_addr->sin_family != AF_INET) {
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

int send_all_or_shutdown_helper(int fd, const void *send_buf, size_t len, const sigset_t *wait_signal_mask)
{
    const char *data = (const char *)send_buf;
    size_t sent_total = 0;
    ssize_t sent_bytes;

    while (sent_total < len) {
        if (wait_ready_or_shutdown_helper(fd, POLLOUT, wait_signal_mask) < 0) {
            return -1;
        }

        sent_bytes = send(fd, data + sent_total, len - sent_total, MSG_NOSIGNAL);
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

int format_addr_helper(const struct sockaddr_in *addr, char *out, size_t len)
{
    char host[INET_ADDRSTRLEN] = { 0 };

    if (inet_ntop(AF_INET, &addr->sin_addr, host, sizeof(host)) == NULL) {
        return -1;
    }
    if (snprintf(out, len, "%s:%u", host, ntohs(addr->sin_port)) < 0) {
        return -1;
    }
    return 0;
}

int format_peer_helper(int fd, char *out, size_t len)
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
    return format_addr_helper(&peer_addr, out, len);
}

int register_signal_handler_helper(int signal_number, void (*handler)(int), int flags)
{
    struct sigaction sa = { 0 };

    sa.sa_handler = handler;
    sa.sa_flags = flags;
    if (sigemptyset(&sa.sa_mask) < 0) {
        return -1;
    }
    return sigaction(signal_number, &sa, NULL);
}

int ignore_sigpipe_helper(void)
{
    return register_signal_handler_helper(SIGPIPE, SIG_IGN, 0);
}

int catch_shutdown_signals_helper(sigset_t *wait_signal_mask)
{
    static const int shutdown_signals[] = { SIGINT, SIGTERM };
    sigset_t signals_to_block;
    size_t i;
    int mask_error_code;

    if (sigemptyset(&signals_to_block) < 0) {
        return -1;
    }

    for (i = 0; i < sizeof(shutdown_signals) / sizeof(shutdown_signals[0]); ++i) {
        if (register_signal_handler_helper(shutdown_signals[i], on_shutdown_signal_helper, 0) < 0) {
            return -1;
        }
        if (sigaddset(&signals_to_block, shutdown_signals[i]) < 0) {
            return -1;
        }
    }

    mask_error_code = pthread_sigmask(SIG_BLOCK, &signals_to_block, wait_signal_mask);
    if (mask_error_code != 0) {
        errno = mask_error_code;
        return -1;
    }
    return 0;
}

int should_shut_down_helper(void)
{
    return g_shutdown_signal_number != 0;
}

int which_shutdown_signal_helper(void)
{
    return (int)g_shutdown_signal_number;
}
