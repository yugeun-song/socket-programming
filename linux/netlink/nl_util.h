#ifndef NL_UTIL_H_HELPER
#define NL_UTIL_H_HELPER

#include <signal.h>
#include <stddef.h>

#include <sys/types.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define NL_BUF_SIZE_HELPER (64 * 1024)

int nl_open_helper(int protocol);
int nl_join_group_helper(int fd, unsigned int group);
int nl_set_rcvbuf_helper(int fd, int bytes);

int nl_send_helper(int fd, const void *msg, size_t len);
ssize_t nl_recv_helper(int fd, void *recv_buf, size_t len);
ssize_t nl_recv_or_shutdown_helper(int fd, void *recv_buf, size_t len, const sigset_t *wait_signal_mask);

int nl_add_attr_helper(struct nlmsghdr *nl_header, size_t capacity, unsigned short type, const void *data,
                       unsigned short len);
void nl_parse_attrs_helper(struct rtattr *attr, int len, struct rtattr **attr_table, unsigned short max);

int nl_check_error_helper(const struct nlmsghdr *nl_header);
int nl_check_done_helper(const struct nlmsghdr *nl_header);
unsigned int nl_next_seq_helper(void);

#endif