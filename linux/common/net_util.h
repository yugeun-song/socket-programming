#ifndef NET_UTIL_H
#define NET_UTIL_H

#include <signal.h>
#include <stddef.h>

#include <sys/socket.h>

#define SIGNAL_INTERRUPTS 0
#define SIGNAL_RESTARTS SA_RESTART

int tcp_listen(unsigned short port, int backlog, int level, int optname, const void *optval, socklen_t optlen);
int tcp_connect(const char *host, unsigned short port);
int tcp_accept(int listen_fd);

int udp_bind(unsigned short port, int level, int optname, const void *optval, socklen_t optlen);
int udp_connect(const char *host, unsigned short port);

int send_all(int fd, const void *buf, size_t len);
int set_nonblocking(int fd, int enable);

int ignore_sigpipe(void);
int install_signal_handler(int signo, void (*handler)(int), int flags);
int block_signals(const int *signos, size_t count, sigset_t *saved);

#endif