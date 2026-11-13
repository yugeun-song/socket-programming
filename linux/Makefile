STD := c99
CPPFLAGS_EXTRA := -I.
CFLAGS_EXTRA := -pthread
LDLIBS := -pthread

SRC_DIRS := tcp udp zerocopy netlink epoll unix_domain
LIB_SRCS := $(wildcard common/*.c) netlink/nl_util.c
PROGRAMS := $(patsubst %.c,%,$(filter-out $(LIB_SRCS),$(wildcard $(addsuffix /*.c,$(SRC_DIRS)))))

include build.mk