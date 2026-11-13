#ifndef NETLINK_NL_UTIL_H
#define NETLINK_NL_UTIL_H

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

#define NL_BUF_SIZE_HELPER (64 * 1024)

int nl_open_helper(int protocol);
int nl_join_group_helper(int fd, unsigned int group);
int nl_set_rcvbuf_helper(int fd, int bytes);

int nl_send_helper(int fd, const void *const msg, size_t len);
ssize_t nl_recv_or_shutdown_helper(int fd, void *const mut_recv_buf, size_t len,
                                   const sigset_t *const wait_signal_mask_nullable);

int nl_add_attr_helper(struct nlmsghdr *const mut_nl_header, size_t capacity, unsigned short type,
                       const void *const data_nullable, unsigned short len);
void nl_parse_attrs_helper(struct rtattr **const mut_attr_table, unsigned short max,
                           const struct rtattr *const attrs, int len);

int nl_check_error_helper(const struct nlmsghdr *const nl_header);
int nl_check_done_helper(const struct nlmsghdr *const nl_header);
unsigned int nl_get_next_seq_helper(void);

#endif /* NETLINK_NL_UTIL_H */
