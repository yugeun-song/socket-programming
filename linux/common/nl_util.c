#define _GNU_SOURCE

#include "common/nl_util.h"

#include <errno.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

int nl_open(int protocol)
{
    struct sockaddr_nl addr = { 0 };
    int saved_errno;
    int fd;

    fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, protocol);
    if (fd < 0) {
        return -1;
    }

    addr.nl_family = AF_NETLINK;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    return fd;
}

int nl_join_group(int fd, unsigned int group)
{
    return setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &group, sizeof(group));
}

int nl_send(int fd, const void *msg, size_t len)
{
    struct sockaddr_nl kernel = { 0 };
    ssize_t n;

    kernel.nl_family = AF_NETLINK;

    while (1) {
        n = sendto(fd, msg, len, MSG_NOSIGNAL, (struct sockaddr *)&kernel, sizeof(kernel));
        if (n >= 0) {
            break;
        }
        if (errno != EINTR) {
            return -1;
        }
    }

    if ((size_t)n != len) {
        errno = EMSGSIZE;
        return -1;
    }
    return 0;
}

ssize_t nl_recv(int fd, void *buf, size_t len)
{
    struct sockaddr_nl from = { 0 };
    socklen_t fromlen = sizeof(from);
    ssize_t n;

    n = recvfrom(fd, buf, len, MSG_TRUNC, (struct sockaddr *)&from, &fromlen);
    if (n < 0) {
        return -1;
    }
    if (fromlen != sizeof(from) || from.nl_pid != 0) {
        errno = EBADMSG;
        return -1;
    }
    if ((size_t)n > len) {
        errno = EMSGSIZE;
        return -1;
    }
    return n;
}

int nl_add_attr(struct nlmsghdr *nlh, size_t cap, unsigned short type, const void *data, unsigned short len)
{
    struct rtattr *rta;
    size_t need;
    size_t pad;

    need = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_SPACE(len);
    if (need > cap) {
        errno = ENOSPC;
        return -1;
    }

    rta = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = (unsigned short)RTA_LENGTH(len);
    if (len > 0) {
        memcpy(RTA_DATA(rta), data, len);
    }

    pad = RTA_SPACE(len) - RTA_LENGTH(len);
    if (pad > 0) {
        memset((char *)RTA_DATA(rta) + len, 0, pad);
    }

    nlh->nlmsg_len = (unsigned int)need;
    return 0;
}

void nl_parse_attrs(struct rtattr *rta, int len, struct rtattr **tb, unsigned short max)
{
    unsigned short type;

    memset(tb, 0, sizeof(*tb) * ((size_t)max + 1));

    while (RTA_OK(rta, len)) {
        type = (unsigned short)(rta->rta_type & NLA_TYPE_MASK);
        if (type <= max && tb[type] == NULL) {
            tb[type] = rta;
        }
        rta = RTA_NEXT(rta, len);
    }
}

int nl_check_error(const struct nlmsghdr *nlh)
{
    const struct nlmsgerr *err;

    if (nlh->nlmsg_type != NLMSG_ERROR) {
        return 0;
    }
    if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*err))) {
        errno = EBADMSG;
        return -1;
    }

    err = (const struct nlmsgerr *)NLMSG_DATA(nlh);
    if (err->error == 0) {
        return 0;
    }

    errno = -err->error;
    return -1;
}

unsigned int nl_next_seq(void)
{
    static atomic_uint counter;

    return atomic_fetch_add_explicit(&counter, 1, memory_order_relaxed) + 1;
}