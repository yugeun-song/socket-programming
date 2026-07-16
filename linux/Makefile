CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -pedantic -ggdb3 -Og \
          -fno-omit-frame-pointer -fno-optimize-sibling-calls \
          -fasynchronous-unwind-tables -I.
LDFLAGS = -pthread

ifdef GPROF
CFLAGS  += -pg
LDFLAGS += -pg
endif

BIN_DIR  = bin
LIB_DIRS = common
SRC_DIRS = tcp udp epoll unix_domain

LIB_SRCS = $(wildcard $(addsuffix /*.c,$(LIB_DIRS)))
LIB_OBJS = $(patsubst %.c,$(BIN_DIR)/%.o,$(LIB_SRCS))

EXE_SRCS = $(wildcard $(addsuffix /*.c,$(SRC_DIRS)))
EXES     = $(patsubst %.c,$(BIN_DIR)/%,$(EXE_SRCS))

SYMBOL_SRCS = $(wildcard $(addsuffix /*.c,$(LIB_DIRS) $(SRC_DIRS)) \
                         $(addsuffix /*.h,$(LIB_DIRS) $(SRC_DIRS)))

.PHONY: all clean tags cscope symbols
all: $(EXES)

$(BIN_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/%: %.c $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@ $(LDFLAGS)

tags:
	ctags -f tags $(SYMBOL_SRCS)

cscope:
	printf '%s\n' $(SYMBOL_SRCS) > cscope.files
	cscope -bq -i cscope.files

symbols: tags cscope

clean:
	rm -rf $(BIN_DIR) tags cscope.out cscope.in.out cscope.po.out cscope.files