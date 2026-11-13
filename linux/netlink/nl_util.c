#define _DEFAULT_SOURCE

#include "netlink/nl_util.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/net_util.h"

static ssize_t nl_recv_helper(int fd, void *const mut_recv_buf, size_t len)
{
    struct sockaddr_nl from = { 0 };
    socklen_t from_len = sizeof(from);

    ssize_t received_bytes = recvfrom(fd, mut_recv_buf, len, MSG_TRUNC, (struct sockaddr *)&from,
                                      &from_len);
    if (received_bytes < 0) {
        return -1;
    }
    if (from_len != sizeof(from) || from.nl_pid != 0) {
        errno = EBADMSG;
        return -1;
    }
    if ((size_t)received_bytes > len) {
        errno = EMSGSIZE;
        return -1;
    }
    return received_bytes;
}

int nl_open_helper(int protocol)
{
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, protocol);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_nl addr = {
        .nl_family = AF_NETLINK
    };

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        goto out_close_fd;
    }

    return fd;

out_close_fd:
    close_keeping_errno_helper(fd);
    return -1;
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

int nl_send_helper(int fd, const void *const msg, size_t len)
{
    struct sockaddr_nl kernel = {
        .nl_family = AF_NETLINK
    };
    ssize_t sent_bytes;

    for (;;) {
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

ssize_t nl_recv_or_shutdown_helper(int fd, void *const mut_recv_buf, size_t len,
                                   const sigset_t *const wait_signal_mask_nullable)
{
    if (wait_ready_or_shutdown_helper(fd, POLLIN, wait_signal_mask_nullable) < 0) {
        return -1;
    }
    return nl_recv_helper(fd, mut_recv_buf, len);
}

int nl_add_attr_helper(struct nlmsghdr *const mut_nl_header, size_t capacity, unsigned short type,
                       const void *const data_nullable, unsigned short len)
{
    size_t header_space = NLMSG_ALIGN(mut_nl_header->nlmsg_len);
    size_t needed_size = header_space + RTA_SPACE(len);
    if (needed_size > capacity) {
        errno = ENOBUFS;
        return -1;
    }

    struct rtattr *attr = (struct rtattr *)((char *)mut_nl_header + header_space);
    unsigned char *payload = RTA_DATA(attr);

    attr->rta_type = type;
    attr->rta_len = (unsigned short)RTA_LENGTH(len);
    if (len > 0) {
        memcpy(payload, data_nullable, len);
    }

    size_t pad_bytes = RTA_SPACE(len) - RTA_LENGTH(len);
    if (pad_bytes > 0) {
        memset(payload + len, 0, pad_bytes);
    }

    mut_nl_header->nlmsg_len = (unsigned int)needed_size;
    return 0;
}

void nl_parse_attrs_helper(const struct rtattr *const attrs, int len,
                           struct rtattr **const mut_attr_table, unsigned short max)
{
    memset(mut_attr_table, 0, sizeof(*mut_attr_table) * ((size_t)max + 1));

    for (const struct rtattr *attr = attrs; RTA_OK(attr, len); attr = RTA_NEXT(attr, len)) {
        unsigned short type = (unsigned short)(attr->rta_type & NLA_TYPE_MASK);

        if (type <= max && mut_attr_table[type] == NULL) {
            mut_attr_table[type] = (struct rtattr *)attr;
        }
    }
}

int nl_check_error_helper(const struct nlmsghdr *const nl_header)
{
    if (nl_header->nlmsg_type != NLMSG_ERROR) {
        return 0;
    }

    const struct nlmsgerr *error_msg = NLMSG_DATA(nl_header);

    if (nl_header->nlmsg_len < NLMSG_LENGTH(sizeof(*error_msg))) {
        errno = EBADMSG;
        return -1;
    }
    if (error_msg->error == 0) {
        return 0;
    }

    errno = -error_msg->error;
    return -1;
}

int nl_check_done_helper(const struct nlmsghdr *const nl_header)
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
    static pthread_mutex_t s_counter_lock = PTHREAD_MUTEX_INITIALIZER;
    static unsigned int s_counter;

    pthread_mutex_lock(&s_counter_lock);
    ++s_counter;
    unsigned int seq = s_counter;
    pthread_mutex_unlock(&s_counter_lock);

    return seq;
}
