#ifndef COMMON_NET_UTIL_H
#define COMMON_NET_UTIL_H

#include <netinet/in.h>
#include <signal.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>

#define ADDR_TEXT_MAX_HELPER 32

void close_keeping_errno_helper(int fd);

int tcp_listen_helper(unsigned short port, int backlog);
int tcp_connect_helper(const char *const host, unsigned short port,
                       const sigset_t *const wait_signal_mask_nullable);
int tcp_accept_helper(int server_fd, struct sockaddr_in *const mut_peer_addr,
                      const sigset_t *const wait_signal_mask_nullable);

int udp_bind_helper(unsigned short port);
int udp_connect_helper(const char *const host, unsigned short port);

int wait_ready_or_shutdown_helper(int fd, short events,
                                  const sigset_t *const wait_signal_mask_nullable);
ssize_t recv_or_shutdown_helper(int fd, void *const mut_recv_buf, size_t len,
                                const sigset_t *const wait_signal_mask_nullable);
ssize_t recvfrom_or_shutdown_helper(int fd, void *const mut_recv_buf, size_t len,
                                    struct sockaddr_in *const mut_peer_addr,
                                    const sigset_t *const wait_signal_mask_nullable);
int send_all_or_shutdown_helper(int fd, const void *const send_buf, size_t len,
                                const sigset_t *const wait_signal_mask_nullable);

int format_addr_helper(char *const mut_out, size_t len, const struct sockaddr_in *const addr);
int format_peer_helper(int fd, char *const mut_out, size_t len);

int ignore_sigpipe_helper(void);
int register_signal_handler_helper(int signal_number, void (*const handler)(int), int flags);
int catch_shutdown_signals_helper(sigset_t *const mut_wait_signal_mask);
int should_shut_down_helper(void);
int get_shutdown_signal_helper(void);

#endif /* COMMON_NET_UTIL_H */
