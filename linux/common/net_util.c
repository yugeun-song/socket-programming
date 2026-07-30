#define _GNU_SOURCE

#include "common/net_util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

static volatile sig_atomic_t g_stop_requested;

static void on_stop(int signo __attribute__((unused)))
{
    g_stop_requested = 1;
}

static int bind_inet(int type, unsigned short port, int level, int optname, const void *optval, socklen_t optlen)
{
    struct sockaddr_in addr = { 0 };
    int reuse_enable = 1;
    int saved_errno;
    int fd;

    fd = socket(AF_INET, type | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return -1;
    }

    if (optval == NULL || optlen == 0) {
        level = SOL_SOCKET;
        optname = SO_REUSEADDR;
        optval = &reuse_enable;
        optlen = sizeof(reuse_enable);
    }
    if (setsockopt(fd, level, optname, optval, optlen) < 0) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    return fd;
}

static int wait_connected(int fd, const struct deadline *dl)
{
    socklen_t errlen = sizeof(int);
    int err = 0;

    if (wait_ready(fd, POLLOUT, dl, NULL) < 0) {
        return -1;
    }
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0) {
        return -1;
    }
    if (err != 0) {
        errno = err;
        return -1;
    }
    return 0;
}

static int connect_inet(int type, const char *host, unsigned short port, const struct deadline *dl)
{
    struct sockaddr_in addr = { 0 };
    int saved_errno;
    int fd;

    fd = socket(AF_INET, type | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        errno = EINVAL;
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS && errno != EINTR) {
            saved_errno = errno;
            close(fd);
            errno = saved_errno;
            return -1;
        }
        if (wait_connected(fd, dl) < 0) {
            saved_errno = errno;
            close(fd);
            errno = saved_errno;
            return -1;
        }
    }

    return fd;
}

int tcp_listen(unsigned short port, int backlog, int level, int optname, const void *optval, socklen_t optlen)
{
    int saved_errno;
    int fd;

    fd = bind_inet(SOCK_STREAM, port, level, optname, optval, optlen);
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

int tcp_connect(const char *host, unsigned short port, const struct deadline *dl)
{
    return connect_inet(SOCK_STREAM, host, port, dl);
}

int tcp_accept(int listen_fd, struct sockaddr_in *peer, const struct deadline *dl, const sigset_t *mask)
{
    socklen_t peerlen;
    int fd;

    while (1) {
        if (wait_ready(listen_fd, POLLIN, dl, mask) < 0) {
            return -1;
        }

        peerlen = sizeof(*peer);
        fd = accept4(listen_fd, (struct sockaddr *)peer, &peerlen, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd >= 0) {
            break;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED) {
            return -1;
        }
    }

    if (peerlen != sizeof(*peer) || peer->sin_family != AF_INET) {
        close(fd);
        errno = EAFNOSUPPORT;
        return -1;
    }

    return fd;
}

int udp_bind(unsigned short port, int level, int optname, const void *optval, socklen_t optlen)
{
    return bind_inet(SOCK_DGRAM, port, level, optname, optval, optlen);
}

int udp_connect(const char *host, unsigned short port)
{
    struct deadline dl;

    if (deadline_start(&dl, DEADLINE_FOREVER) < 0) {
        return -1;
    }
    return connect_inet(SOCK_DGRAM, host, port, &dl);
}

int wait_ready(int fd, short events, const struct deadline *dl, const sigset_t *mask)
{
    int ready;

    while (1) {
        if (stop_requested()) {
            errno = ECANCELED;
            return -1;
        }
        if (deadline_expired(dl)) {
            errno = ETIMEDOUT;
            return -1;
        }

        ready = poll_until(fd, events, dl, mask);
        if (ready > 0) {
            return ready;
        }
        if (ready == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
}

ssize_t recv_until(int fd, void *buf, size_t len, const struct deadline *dl, const sigset_t *mask)
{
    ssize_t n;

    while (1) {
        if (wait_ready(fd, POLLIN, dl, mask) < 0) {
            return -1;
        }

        n = recv(fd, buf, len, 0);
        if (n >= 0) {
            return n;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
    }
}

ssize_t recvfrom_until(int fd, void *buf, size_t len, struct sockaddr_in *peer, const struct deadline *dl, const sigset_t *mask)
{
    socklen_t peerlen;
    ssize_t n;

    while (1) {
        if (wait_ready(fd, POLLIN, dl, mask) < 0) {
            return -1;
        }

        peerlen = sizeof(*peer);
        n = recvfrom(fd, buf, len, 0, (struct sockaddr *)peer, &peerlen);
        if (n >= 0) {
            if (peerlen != sizeof(*peer) || peer->sin_family != AF_INET) {
                errno = EAFNOSUPPORT;
                return -1;
            }
            return n;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
    }
}

int send_all_until(int fd, const void *buf, size_t len, const struct deadline *dl, const sigset_t *mask)
{
    const char *p = (const char *)buf;
    size_t sent = 0;
    ssize_t n;

    while (sent < len) {
        if (wait_ready(fd, POLLOUT, dl, mask) < 0) {
            return -1;
        }

        n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int set_nonblocking(int fd, int enable)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(fd, F_SETFL, flags);
}

int format_addr(const struct sockaddr_in *addr, char *out, size_t len)
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

int format_peer(int fd, char *out, size_t len)
{
    struct sockaddr_in addr = { 0 };
    socklen_t addrlen = sizeof(addr);

    if (getpeername(fd, (struct sockaddr *)&addr, &addrlen) < 0) {
        return -1;
    }
    if (addrlen != sizeof(addr) || addr.sin_family != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    return format_addr(&addr, out, len);
}

int install_signal_handler(int signo, void (*handler)(int), int flags)
{
    struct sigaction sa = { 0 };

    sa.sa_handler = handler;
    sa.sa_flags = flags;
    if (sigemptyset(&sa.sa_mask) < 0) {
        return -1;
    }
    return sigaction(signo, &sa, NULL);
}

int ignore_sigpipe(void)
{
    return install_signal_handler(SIGPIPE, SIG_IGN, SIGNAL_INTERRUPTS);
}

int install_stop_handlers(sigset_t *saved)
{
    static const int stop_signals[] = { SIGINT, SIGTERM };

    if (install_signal_handler(SIGINT, on_stop, SIGNAL_INTERRUPTS) < 0) {
        return -1;
    }
    if (install_signal_handler(SIGTERM, on_stop, SIGNAL_INTERRUPTS) < 0) {
        return -1;
    }
    return block_signals(stop_signals, sizeof(stop_signals) / sizeof(stop_signals[0]), saved);
}

int block_signals(const int *signos, size_t count, sigset_t *saved)
{
    sigset_t blocked;
    size_t i;

    if (sigemptyset(&blocked) < 0) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (sigaddset(&blocked, signos[i]) < 0) {
            return -1;
        }
    }
    return pthread_sigmask(SIG_BLOCK, &blocked, saved);
}

int stop_requested(void)
{
    return g_stop_requested != 0;
}