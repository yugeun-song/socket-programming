#define _GNU_SOURCE

#include "netlink/nl_util.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include "common/net_util.h"

int nl_open_helper(int protocol)
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

int nl_join_group_helper(int fd, unsigned int group)
{
    return setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &group, sizeof(group));
}

int nl_set_rcvbuf_helper(int fd, int bytes)
{
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &bytes, sizeof(bytes)) == 0) {
        return 0;
    }
    return setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes));
}

int nl_send_helper(int fd, const void *msg, size_t len)
{
    struct sockaddr_nl kernel = { 0 };
    ssize_t sent_bytes;

    kernel.nl_family = AF_NETLINK;

    while (1) {
        sent_bytes = sendto(fd, msg, len, MSG_NOSIGNAL, (struct sockaddr *)&kernel, sizeof(kernel));
        if (sent_bytes >= 0) {
            break;
        }
        if (errno != EINTR) {
            return -1;
        }
    }

    if ((size_t)sent_bytes != len) {
        errno = EMSGSIZE;
        return -1;
    }
    return 0;
}

ssize_t nl_recv_helper(int fd, void *nl_recv_buf, size_t len)
{
    struct sockaddr_nl from = { 0 };
    socklen_t fromlen = sizeof(from);
    ssize_t received_bytes;

    received_bytes = recvfrom(fd, nl_recv_buf, len, MSG_TRUNC, (struct sockaddr *)&from, &fromlen);
    if (received_bytes < 0) {
        return -1;
    }
    if (fromlen != sizeof(from) || from.nl_pid != 0) {
        errno = EBADMSG;
        return -1;
    }
    if ((size_t)received_bytes > len) {
        errno = EMSGSIZE;
        return -1;
    }
    return received_bytes;
}

ssize_t nl_recv_or_shutdown_helper(int fd, void *nl_recv_buf, size_t len, const sigset_t *wait_signal_mask)
{
    if (wait_ready_or_shutdown_helper(fd, POLLIN, wait_signal_mask) < 0) {
        return -1;
    }
    return nl_recv_helper(fd, nl_recv_buf, len);
}

int nl_add_attr_helper(struct nlmsghdr *nl_header, size_t capacity, unsigned short type, const void *data,
                       unsigned short len)
{
    struct rtattr *attr;
    size_t needed_size;
    size_t pad_bytes;

    needed_size = NLMSG_ALIGN(nl_header->nlmsg_len) + RTA_SPACE(len);
    if (needed_size > capacity) {
        errno = ENOBUFS;
        return -1;
    }

    attr = (struct rtattr *)((char *)nl_header + NLMSG_ALIGN(nl_header->nlmsg_len));
    attr->rta_type = type;
    attr->rta_len = (unsigned short)RTA_LENGTH(len);
    if (len > 0) {
        memcpy(RTA_DATA(attr), data, len);
    }

    pad_bytes = RTA_SPACE(len) - RTA_LENGTH(len);
    if (pad_bytes > 0) {
        memset((char *)RTA_DATA(attr) + len, 0, pad_bytes);
    }

    nl_header->nlmsg_len = (unsigned int)needed_size;
    return 0;
}

void nl_parse_attrs_helper(struct rtattr *attr, int len, struct rtattr **attr_table, unsigned short max)
{
    unsigned short type;

    memset(attr_table, 0, sizeof(*attr_table) * ((size_t)max + 1));

    while (RTA_OK(attr, len)) {
        type = (unsigned short)(attr->rta_type & NLA_TYPE_MASK);
        if (type <= max && attr_table[type] == NULL) {
            attr_table[type] = attr;
        }
        attr = RTA_NEXT(attr, len);
    }
}

int nl_check_error_helper(const struct nlmsghdr *nl_header)
{
    const struct nlmsgerr *error_msg;

    if (nl_header->nlmsg_type != NLMSG_ERROR) {
        return 0;
    }
    if (nl_header->nlmsg_len < NLMSG_LENGTH(sizeof(*error_msg))) {
        errno = EBADMSG;
        return -1;
    }

    error_msg = (const struct nlmsgerr *)NLMSG_DATA(nl_header);
    if (error_msg->error == 0) {
        return 0;
    }

    errno = -error_msg->error;
    return -1;
}

int nl_check_done_helper(const struct nlmsghdr *nl_header)
{
    int error_code;

    if (nl_header->nlmsg_len < NLMSG_LENGTH(sizeof(error_code))) {
        return 0;
    }

    memcpy(&error_code, NLMSG_DATA(nl_header), sizeof(error_code));
    if (error_code == 0) {
        return 0;
    }

    errno = -error_code;
    return -1;
}

unsigned int nl_next_seq_helper(void)
{
    static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
    static unsigned int counter;
    unsigned int seq;

    pthread_mutex_lock(&counter_lock);
    counter += 1;
    seq = counter;
    pthread_mutex_unlock(&counter_lock);

    return seq;
}