#ifndef NL_UTIL_H
#define NL_UTIL_H

#include <stddef.h>

#include <sys/types.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "common/deadline.h"

#define NL_BUF_SIZE (64 * 1024)

int nl_open(int protocol);
int nl_join_group(int fd, unsigned int group);
int nl_set_rcvbuf(int fd, int bytes);

int nl_send(int fd, const void *msg, size_t len);
ssize_t nl_recv(int fd, void *buf, size_t len);
ssize_t nl_recv_until(int fd, void *buf, size_t len, const struct deadline *dl, const sigset_t *mask);

int nl_add_attr(struct nlmsghdr *nlh, size_t cap, unsigned short type, const void *data, unsigned short len);
void nl_parse_attrs(struct rtattr *rta, int len, struct rtattr **tb, unsigned short max);

int nl_check_error(const struct nlmsghdr *nlh);
int nl_check_done(const struct nlmsghdr *nlh);
unsigned int nl_next_seq(void);

#endif