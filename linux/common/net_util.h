#ifndef NET_UTIL_H_HELPER
#define NET_UTIL_H_HELPER

#include <signal.h>
#include <stddef.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <netinet/in.h>

#define ADDR_TEXT_MAX_HELPER 32

int tcp_listen_helper(unsigned short port, int backlog);
int tcp_connect_helper(const char *host, unsigned short port);
int tcp_accept_helper(int server_fd, struct sockaddr_in *peer_addr, const sigset_t *wait_signal_mask);

int udp_bind_helper(unsigned short port);
int udp_connect_helper(const char *host, unsigned short port);

int wait_ready_or_shutdown_helper(int fd, short events, const sigset_t *wait_signal_mask);
ssize_t recv_or_shutdown_helper(int fd, void *recv_buf, size_t len, const sigset_t *wait_signal_mask);
ssize_t recvfrom_or_shutdown_helper(int fd, void *recv_buf, size_t len, struct sockaddr_in *peer_addr,
                                    const sigset_t *wait_signal_mask);
int send_all_or_shutdown_helper(int fd, const void *send_buf, size_t len, const sigset_t *wait_signal_mask);

int format_addr_helper(const struct sockaddr_in *addr, char *out, size_t len);
int format_peer_helper(int fd, char *out, size_t len);

int ignore_sigpipe_helper(void);
int register_signal_handler_helper(int signal_number, void (*handler)(int), int flags);
int catch_shutdown_signals_helper(sigset_t *wait_signal_mask);
int should_shut_down_helper(void);
int which_shutdown_signal_helper(void);

#endif
