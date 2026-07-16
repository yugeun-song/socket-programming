#ifndef NET_UTIL_H
#define NET_UTIL_H

#include <stddef.h>
#include <sys/socket.h>

int tcp_listen(unsigned short port, int backlog, int level, int optname, const void *optval, socklen_t optlen);

int tcp_connect(const char *host, unsigned short port);

int udp_bind(unsigned short port, int level, int optname, const void *optval, socklen_t optlen);

int udp_connect(const char *host, unsigned short port);

int send_all(int fd, const void *buf, size_t len);

int set_nonblocking(int fd, int enable);

#endif