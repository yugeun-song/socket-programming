#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include <linux/genetlink.h>
#include <linux/netlink.h>

#include "common/log.h"
#include "common/nl_util.h"

static unsigned int count_nested(struct rtattr *nest)
{
    struct rtattr *rta;
    unsigned int count = 0;
    int len;

    rta = (struct rtattr *)RTA_DATA(nest);
    len = (int)RTA_PAYLOAD(nest);

    while (RTA_OK(rta, len)) {
        ++count;
        rta = RTA_NEXT(rta, len);
    }

    return count;
}

static void print_family(struct nlmsghdr *nlh)
{
    struct rtattr *tb[CTRL_ATTR_MAX + 1];
    const char *name = "?";
    unsigned int version = 0;
    unsigned int groups = 0;
    unsigned short id = 0;

    if (nlh->nlmsg_len < NLMSG_LENGTH(GENL_HDRLEN)) {
        return;
    }

    nl_parse_attrs((struct rtattr *)((char *)NLMSG_DATA(nlh) + GENL_HDRLEN), (int)NLMSG_PAYLOAD(nlh, GENL_HDRLEN), tb, CTRL_ATTR_MAX);

    if (tb[CTRL_ATTR_FAMILY_NAME] != NULL) {
        name = (const char *)RTA_DATA(tb[CTRL_ATTR_FAMILY_NAME]);
    }
    if (tb[CTRL_ATTR_FAMILY_ID] != NULL) {
        memcpy(&id, RTA_DATA(tb[CTRL_ATTR_FAMILY_ID]), sizeof(id));
    }
    if (tb[CTRL_ATTR_VERSION] != NULL) {
        memcpy(&version, RTA_DATA(tb[CTRL_ATTR_VERSION]), sizeof(version));
    }
    if (tb[CTRL_ATTR_MCAST_GROUPS] != NULL) {
        groups = count_nested(tb[CTRL_ATTR_MCAST_GROUPS]);
    }

    printf("genl_family_list: id %-4u v%-2u %-20s %u multicast groups\n", id, version, name, groups);
}

int main(void)
{
    struct {
        struct nlmsghdr nlh;
        struct genlmsghdr genl;
    } req = { 0 };
    char buf[NL_BUF_SIZE];
    struct nlmsghdr *nlh;
    unsigned int seq;
    unsigned int families = 0;
    ssize_t n;
    int done = 0;
    int len;
    int fd;

    fd = nl_open(NETLINK_GENERIC);
    if (fd < 0) {
        log_errno("nl_open(NETLINK_GENERIC)");
        return 1;
    }

    seq = nl_next_seq();
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(req.genl));
    req.nlh.nlmsg_type = GENL_ID_CTRL;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = seq;
    req.genl.cmd = CTRL_CMD_GETFAMILY;
    req.genl.version = 1;

    if (nl_send(fd, &req, req.nlh.nlmsg_len) < 0) {
        log_errno("nl_send()");
        close(fd);
        return 1;
    }

    while (!done) {
        n = nl_recv(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_errno("nl_recv()");
            close(fd);
            return 1;
        }

        len = (int)n;
        nlh = (struct nlmsghdr *)buf;

        while (NLMSG_OK(nlh, len)) {
            if (nlh->nlmsg_seq != seq) {
                nlh = NLMSG_NEXT(nlh, len);
                continue;
            }
            if (nlh->nlmsg_flags & NLM_F_DUMP_INTR) {
                log_msg("dump raced with a family change, result is incomplete");
                close(fd);
                return 1;
            }
            if (nlh->nlmsg_type == NLMSG_DONE) {
                done = 1;
                break;
            }
            if (nl_check_error(nlh) < 0) {
                log_errno("CTRL_CMD_GETFAMILY");
                close(fd);
                return 1;
            }
            if (nlh->nlmsg_type == GENL_ID_CTRL) {
                print_family(nlh);
                ++families;
            }
            nlh = NLMSG_NEXT(nlh, len);
        }
    }

    printf("genl_family_list: %u generic netlink families\n", families);

    close(fd);
    return 0;
}