CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -ggdb3 -Og \
          -fno-omit-frame-pointer -fno-optimize-sibling-calls \
          -fasynchronous-unwind-tables -MMD -MP -I.
LDFLAGS = -pthread
CTAGS   = ctags
CSCOPE  = cscope

ifdef GPROF
CFLAGS  += -pg
LDFLAGS += -pg
endif

BIN_DIR  = bin
LIB_DIRS = common
SRC_DIRS = tcp udp zerocopy netlink epoll unix_domain

TOPIC_LIB_SRCS = netlink/nl_util.c

LIB_SRCS = $(wildcard $(addsuffix /*.c,$(LIB_DIRS))) $(TOPIC_LIB_SRCS)
LIB_OBJS = $(patsubst %.c,$(BIN_DIR)/%.o,$(LIB_SRCS))

EXE_SRCS = $(filter-out $(TOPIC_LIB_SRCS),$(wildcard $(addsuffix /*.c,$(SRC_DIRS))))
EXES     = $(patsubst %.c,$(BIN_DIR)/%,$(EXE_SRCS))

DEPS = $(LIB_OBJS:.o=.d) $(patsubst %.c,$(BIN_DIR)/%.d,$(EXE_SRCS))

SYMBOL_DIRS  = $(wildcard $(LIB_DIRS) $(SRC_DIRS))
SYMBOL_SRCS  = $(wildcard $(addsuffix /*.c,$(SYMBOL_DIRS)) \
                          $(addsuffix /*.h,$(SYMBOL_DIRS)))
CTAGS_CONF   = .ctags.d/linux.ctags
COMPDB       = compile_commands.json
COMPDB_FLAGS = $(filter-out -MMD -MP,$(CFLAGS))

.PHONY: all clean symbols cscope compdb
all: $(EXES) tags cscope.out $(COMPDB)

$(BIN_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/%: %.c $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@ $(LDFLAGS)

symbols: tags cscope.out $(COMPDB)
cscope: cscope.out
compdb: $(COMPDB)

tags: $(SYMBOL_SRCS) $(SYMBOL_DIRS) $(CTAGS_CONF)
	$(CTAGS) -f $@ $(SYMBOL_SRCS)

cscope.files: $(SYMBOL_SRCS) $(SYMBOL_DIRS)
	printf '%s\n' $(SYMBOL_SRCS) > $@

cscope.out: cscope.files
	$(CSCOPE) -bkqu -i cscope.files

$(COMPDB): $(SYMBOL_SRCS) $(SYMBOL_DIRS) Makefile
	@{ printf '['; first=1; \
	   emit() { \
	       src=$$1; shift; \
	       [ $$first -eq 1 ] || printf ','; first=0; \
	       printf '\n  { "directory": "%s", "file": "%s", "arguments": [' "$(CURDIR)" "$$src"; \
	       sep=''; for a in "$$@"; do printf '%s"%s"' "$$sep" "$$a"; sep=', '; done; \
	       printf '] }'; \
	   }; \
	   for src in $(LIB_SRCS); do emit $$src $(CC) $(COMPDB_FLAGS) -c $$src -o $(BIN_DIR)/$${src%.c}.o; done; \
	   for src in $(EXE_SRCS); do emit $$src $(CC) $(COMPDB_FLAGS) $(LDFLAGS) $$src -o $(BIN_DIR)/$${src%.c}; done; \
	   printf '\n]\n'; } > $@

clean:
	rm -rf $(BIN_DIR) tags cscope.out cscope.in.out cscope.po.out cscope.files $(COMPDB)

-include $(DEPS)