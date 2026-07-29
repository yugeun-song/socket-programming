#define _GNU_SOURCE

#include "common/net_util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

static inline int bind_inet(int type, unsigned short port, int level, int optname, const void *optval, socklen_t optlen)
{
    struct sockaddr_in addr = { 0 };
    int reuse_enable = 1;
    int saved_errno;
    int fd;

    fd = socket(AF_INET, type | SOCK_CLOEXEC, 0);
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

static int wait_connected(int fd)
{
    struct pollfd pfd = { 0 };
    socklen_t errlen = sizeof(int);
    int err = 0;
    int ready;

    pfd.fd = fd;
    pfd.events = POLLOUT;

    while (1) {
        ready = poll(&pfd, 1, -1);
        if (ready >= 0) {
            break;
        }
        if (errno != EINTR) {
            return -1;
        }
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

static inline int connect_inet(int type, const char *host, unsigned short port)
{
    struct sockaddr_in addr = { 0 };
    int saved_errno;
    int fd;

    fd = socket(AF_INET, type | SOCK_CLOEXEC, 0);
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
        if (errno != EINTR || wait_connected(fd) < 0) {
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

int tcp_connect(const char *host, unsigned short port)
{
    return connect_inet(SOCK_STREAM, host, port);
}

int tcp_accept(int listen_fd)
{
    int fd;

    while (1) {
        fd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
        if (fd >= 0) {
            return fd;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
}

int udp_bind(unsigned short port, int level, int optname, const void *optval, socklen_t optlen)
{
    return bind_inet(SOCK_DGRAM, port, level, optname, optval, optlen);
}

int udp_connect(const char *host, unsigned short port)
{
    return connect_inet(SOCK_DGRAM, host, port);
}

int send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t sent = 0;
    ssize_t n;

    while (sent < len) {
        n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
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