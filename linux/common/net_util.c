#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common/net_util.h"

static inline int bind_inet(int type, unsigned short port, int level, int optname, const void *optval, socklen_t optlen)
{
    int fd = socket(AF_INET, type, 0);
    if (fd < 0) {
        return -1;
    }

    int reuse = 1;
    if (optval == NULL || optlen == 0) {
        level = SOL_SOCKET;
        optname = SO_REUSEADDR;
        optval = &reuse;
        optlen = sizeof(reuse);
    }
    if (setsockopt(fd, level, optname, optval, optlen) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    return fd;
}

static inline int connect_inet(int type, const char *host, unsigned short port)
{
    int fd = socket(AF_INET, type, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        errno = EINVAL;
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    return fd;
}

int tcp_listen(unsigned short port, int backlog, int level, int optname, const void *optval, socklen_t optlen)
{
    int fd = bind_inet(SOCK_STREAM, port, level, optname, optval, optlen);
    if (fd < 0) {
        return -1;
    }

    if (listen(fd, backlog) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    return fd;
}

int tcp_connect(const char *host, unsigned short port)
{
    return connect_inet(SOCK_STREAM, host, port);
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

    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
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
    int flags = fcntl(fd, F_GETFL, 0);
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