#ifndef NET_UTIL_H
#define NET_UTIL_H

#include <signal.h>
#include <stddef.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <netinet/in.h>

#include "common/deadline.h"

#define SIGNAL_INTERRUPTS 0
#define SIGNAL_RESTARTS SA_RESTART

#define PEER_TEXT_MAX 32

int tcp_listen(unsigned short port, int backlog, int level, int optname, const void *optval, socklen_t optlen);
int tcp_connect(const char *host, unsigned short port, const struct deadline *dl);
int tcp_accept(int listen_fd, struct sockaddr_in *peer, const struct deadline *dl, const sigset_t *mask);

int udp_bind(unsigned short port, int level, int optname, const void *optval, socklen_t optlen);
int udp_connect(const char *host, unsigned short port);

int wait_ready(int fd, short events, const struct deadline *dl, const sigset_t *mask);
ssize_t recv_until(int fd, void *buf, size_t len, const struct deadline *dl, const sigset_t *mask);
ssize_t recvfrom_until(int fd, void *buf, size_t len, struct sockaddr_in *peer, const struct deadline *dl, const sigset_t *mask);
int send_all_until(int fd, const void *buf, size_t len, const struct deadline *dl, const sigset_t *mask);
int set_nonblocking(int fd, int enable);

int format_addr(const struct sockaddr_in *addr, char *out, size_t len);
int format_peer(int fd, char *out, size_t len);

int ignore_sigpipe(void);
int install_signal_handler(int signo, void (*handler)(int), int flags);
int install_stop_handlers(sigset_t *saved);
int block_signals(const int *signos, size_t count, sigset_t *saved);
int stop_requested(void);
int stop_signal(void);

#endif