# socket-programming

Socket programming study with the Windows and Linux ecosystems kept fully separate.
There is no cross-platform abstraction layer: each tree is implemented natively;
naming, brace style, line endings, and build system all differ by platform.

## Layout

```
socket-programming/
|-- .editorconfig     root config; branches EOL/final-newline by path
|-- .gitattributes    linux/** = LF, Winsock/** = CRLF
|-- .gitignore        single root ignore file
|-- linux/            native BSD sockets  (snake_case, K&R, LF, make)
`-- Winsock/          native Winsock      (PascalCase, Allman, CRLF, VS 2026)
```

Conventions are enforced per tree because the two toolchains differ:

- `.editorconfig` lives at the **root** and branches by path section (`[Winsock/**]` = CRLF
  + trailing newline). EditorConfig supports in-file path branching, and Visual Studio 2026
  honors a `root = true` editorconfig with these sections.
- `.clang-format` is **per tree** (`linux/.clang-format` = LLVM / K&R braces / `int *p`,
  `Winsock/.clang-format` = Microsoft / Allman braces / `int* p`). clang-format resolves the
  nearest config walking upward and **cannot** branch by path within one file, so a separate
  file per tree is the only way to give each platform its own style.
- Include order in `linux/` is enforced by `IncludeBlocks: Regroup`, not maintained by hand:
  the file's own header, then flat `<...>` headers, `<sys/...>`, `<arpa|net|netinet/...>`,
  `<linux/...>`, and project `"..."` last, alphabetical within each group and one blank line
  between them. Own header first is what proves the header compiles on its own instead of
  leaning on whatever the `.c` happened to include before it, and the groups widen from
  portable to platform-specific. Feature test macros such as `_GNU_SOURCE` stay above the
  whole block because they decide what the headers below will expose.
- `Winsock/.clang-format` keeps `SortIncludes: false` deliberately. `<winsock2.h>` has to be
  seen before `<windows.h>`, so alphabetical order would break that tree.
- Lines stop at 120 columns, stated twice because two tools have to agree: `ColumnLimit` in each
  `.clang-format` and `max_line_length` in `.editorconfig`. `ColumnLimit` alone is not enough. It is
  weighed against `PenaltyExcessCharacter`, and `linux/.clang-format` had that penalty set to 1,
  which made overflowing cheaper than breaking a line, so the limit never bound and seven signatures
  ran past it while clang-format still reported the tree clean.
- When a call has to wrap, it wraps inside the argument list rather than after the `=`.
  `PenaltyBreakAssignment: 100` buys that: at the default of 2 clang-format strands the callee on
  its own line under the assignment, which hides which variable is being written.

Rules no formatter can check, applied to every source in both trees:

- Prefix increment and decrement only: `++i`, never `i++`.
- No side effects buried in a larger expression. Write `foo(); ++i;`, not `foo(i++)`, and never
  `buf[i++]` or `*p++`. Barring a case where nothing else works, an expression reads state and a
  statement changes it.
- Locals are declared in one block at the top of the function body, before the first statement.
  Only side-effect-free initializers stay on the declaration; anything that calls something is
  assigned below.
- One declaration per name per function. No reusing an identifier in sibling blocks and no
  shadowing an outer name.
- Header prototypes are packed by concern with a blank line between groups, never one blank line
  per declaration. The grouping is what carries the information; uniform spacing carries none.
- Macro names are upper case, always. `LOG_MSG_HELPER`, `LOG_ERRNO_HELPER` and `LOG_RESULT_HELPER`
  look like the function calls they are not, unless the case says otherwise; the reader needs to know that `__func__`
  appears out of nowhere and that an argument may be evaluated more than once.
- A variable that holds how much a call moved is named for what it counted: `received_bytes`,
  `sent_bytes`, `moved_bytes`, `received_total`. `n` says only that a number arrived. The same goes
  for other results with a shape: `poll_status`, `left_ms`, `remaining`, `report_signal`,
  `drain_status`.
- Short is right only where the short form is the name everyone already knows: `fd`, `len`, `i`.
  Everywhere else the name spells the thing out, because an abbreviation the reader has to decode is
  not brevity. `nlh` became `nl_header`, `tb` became `attr_table`, `zc` became `zerocopy`. `peer` was
  the worst of them: it reads like a descriptor or a `sockaddr`, and it held neither. The buffer it
  named holds an address rendered for a log line, so it is `log_buf`, and `text_to_send` went to the
  string a client actually sends.
- A name that leaves a question open answers it. A wait here ends either because the socket is ready
  or because a shutdown signal arrived, and nothing else, so the family says so: `recv_or_shutdown_helper`,
  `recvfrom_or_shutdown_helper`, `send_all_or_shutdown_helper`, `nl_recv_or_shutdown_helper`. An earlier
  `_until_deadline` named a clock these calls no longer watch.
- A wrapper keeps the name of the call it wraps, and the variable holding its result says what the
  result is for. `tcp_listen_helper` is named for `listen(2)`, the call it makes, while the descriptor
  it returns is `server_fd`, which pairs with the `client_fd` that `tcp_accept_helper` hands back.
  Renaming it to something like `create_server` would hide the syscall the example exists to show.
- A socket buffer is named for the direction it carries: `recv_buf` where a read fills it,
  `send_buf` where a write drains it, `recvBuf` and `sendBuf` in the Winsock tree. An echo server
  keeps `recv_buf` even though it sends the same bytes straight back, because the name says how the
  bytes got there and `send_all_or_shutdown_helper(client_fd, recv_buf, ...)` then reads as what an echo
  server does. A buffer that is neither says what it is instead: `log_buf` holds an address rendered
  for a log line, `error_buf` is scratch for `strerror_r`.
- A variable read as a yes or no says so in its name: `is_done`, `is_listing`, `has_failed`.
  `done` reads like a verb at the point where the condition is tested.
- Branches of one decision are written as one chain. Two `if`s in a row testing the same value are a
  chain that lost its `else`: negative, zero and positive are three outcomes of a single read, and
  `if / else if` says so where two separate `if`s leave the reader to prove the first always exits.
  A guard that leaves early with its own cleanup stays a separate `if` — it is a precondition rather
  than a branch. Two arms that differ only in what they log and then run the same `break` are an
  `if / else` with one `break`, not two blocks that each end the same way.
- `errno` carries what the kernel reported and nothing else. A value that already means something on
  a descriptor cannot be borrowed for a condition of our own: `ETIMEDOUT` on a TCP socket is the
  kernel saying TCP gave up on the connection, so setting it for "my own wait ran out" made the two
  arrive identically and left the real one unhandled. Our own outcomes travel in the return value,
  and `should_shut_down_helper()` answers whether a shutdown was asked for.
- Whoever receives says who sent. Every program that reads from a socket prints the peer as
  `address:port`, so a reply is evidence of a specific exchange rather than a hopeful echo. The
  netlink programs are exempt: their peer is always the kernel.

Everything under `linux/` is written as if it were one thread among many and as if signals arrive
at any moment, even where the program itself is single threaded and installs no handler:

- Descriptors are created close-on-exec atomically: `SOCK_CLOEXEC`, `O_CLOEXEC`, `pipe2`, `accept4`.
  Opening first and setting `FD_CLOEXEC` afterwards leaves a window in which another thread's
  `fork`/`exec` inherits the descriptor.
- Every blocking call handles `EINTR`. `tcp_accept_helper` and `nl_send_helper` retry internally; loops that need
  to notice a shutdown request let `EINTR` reach them and re-test their shutdown flag.
- `sigaction`, never `signal`. `signal` carries historical SysV/BSD differences, and `sigaction`
  is where `sa_mask` and `SA_RESTART` can be stated explicitly. `register_signal_handler_helper` takes the
  flag as an argument rather than picking one, because the choice belongs to the signal's purpose
  and not to a house default.
- A signal handler stores the signal number it was given, so nothing has to be silenced. A callback
  signature the caller does not choose still hands over a parameter, and the answer is to use it
  rather than to write around the compiler: `which_shutdown_signal_helper()` then tells the program whether `SIGINT`
  or `SIGTERM` ended it, and the shutdown line says which. No `(void)` cast, no unused attribute.
- A handler assigns to a `volatile sig_atomic_t` flag and does nothing else. Part of the reason is
  `errno`: the handler runs on the interrupted thread and shares that thread's `errno`, it does not
  get one of its own. A handler that calls anything which fails overwrites the value the interrupted
  code was about to read, and a single `close()` in a handler was enough to turn a waiting `ERANGE`
  into `EBADF` before the interrupted line ever looked at it. Assigning to a flag calls nothing, so
  there is nothing to save and restore.
- `pthread_sigmask` does not report failure the way the rest of the socket API does. `sigprocmask(2)`
  returns -1 and sets `errno`; `pthread_sigmask(3)` returns the error number itself and leaves
  `errno` alone, so `if (pthread_sigmask(...) < 0)` reads `EINVAL` as success. Both callers assign
  the result to `errno` and return -1, which is what every other function in the tree means by
  failure.
- Which flag a handler sets decides whether `SA_RESTART` belongs: a shutdown signal must interrupt the
  wait, so `route_monitor` installs `SIGINT` and `SIGTERM` with no flags at all; its `SIGUSR1`
  counter report must not tear the wait down, so that one gets `SA_RESTART`, the same way `dd`
  reports progress. `SIG_IGN` takes neither, since no handler runs and there is nothing to restart.
- `SA_RESTART` does not cover everything. It restarts `recv` and friends, but never `poll`, `select`,
  `epoll_wait`, or a socket carrying `SO_RCVTIMEO`. A program blocked in `wait_ready_or_shutdown_helper` therefore sees
  `EINTR` for every signal regardless of the flag, and the flag only documents intent there.
- **A timeout belongs to whoever defines one.** TCP does: the kernel retransmits, eventually gives up
  in `tcp_write_err()`, and hands the application `ETIMEDOUT` out of the next `recv`, `send` or
  `connect`. `TCP_USER_TIMEOUT` only moves that verdict earlier, so recognising `ETIMEDOUT` is the
  whole of the handling. UDP and netlink define no timeout at all — `udp(7)` and `netlink(7)` do not
  mention one and neither `net/ipv4/udp.c` nor `net/netlink/` contains `ETIMEDOUT` — so the programs
  there wait until data arrives or a signal stops them. Nothing in this tree runs a clock of its own.
- Testing a shutdown flag and then blocking is two steps, and a signal that lands between them is lost:
  the handler sets the flag with nothing left to interrupt, and the program waits forever on a quiet
  socket. `wait_ready_or_shutdown_helper` therefore calls `ppoll`, and the long-running programs block the signals they
  handle before entering the loop and hand `ppoll` the original mask. Unblocking and blocking become
  one atomic step, so a signal delivered during the flag test stays pending and fires the moment the
  wait begins.
- Blocking a signal is not discarding it. A blocked `SIGTERM` sits in the pending set, visible as
  `SigPnd` in `/proc/self/status`, until `ppoll` swaps the mask back for the length of that one wait
  and the handler runs there. The delivery point is chosen, not the delivery. `SIGPIPE` is the only
  signal genuinely set to `SIG_IGN`, and that trades a default process kill for an `EPIPE` the write
  path can report; `send` also passes `MSG_NOSIGNAL`, but `splice` and `sendfile` have no such flag.
- **The only call that blocks is `ppoll`.** Every socket is created with `SOCK_NONBLOCK` and `splice`
  additionally passes `SPLICE_F_NONBLOCK`, so `recv`, `send`, `accept4` and `splice` all return at
  once and the wait happens in one place. Polling in front of a blocking call bounds nothing: the
  poll answers "at least one byte fits", and the call then writes megabytes and sleeps inside the
  kernel. Measured on a receiver that never read, one `sendfile()` on a blocking socket held for
  20.001 s; non-blocking, the same call returns in 0.000207 s and the loop sees `EAGAIN`.
- No mutable globals. The one exception is that shutdown flag. A shared counter takes a mutex instead:
  `nl_next_seq_helper` hands out netlink sequence numbers under `PTHREAD_MUTEX_INITIALIZER`. The tree
  builds as `-std=c99`, where `<stdatomic.h>` is not available; an uncontended glibc mutex is a
  userspace compare-exchange with no `futex` call, measured at 2.27 ns against 9.50 ns for a
  `pthread_spinlock_t`.
- Only thread-safe library calls: `strerror_r` not `strerror`, `inet_ntop` not `inet_ntoa`,
  `if_indextoname` into a caller buffer.
- `common/log.c` formats a whole line into a local buffer and emits it with one `fprintf`. Three
  stdio calls per message would let concurrent threads interleave mid-line.

## linux/

TCP and UDP echo, four zero-copy transfer paths, and six netlink programs, built with a Makefile.
Every server that accepts a connection handles one and exits. The UDP echo server has no connection
to end, so it serves datagrams until a signal stops it.

### build

```sh
make -C linux            # binaries at linux/bin/<topic>/<name>, and a current tags and cscope.out
make -C linux clean && make -C linux GPROF=1    # -pg for gprof / mcount-based uftrace
make -C linux symbols    # the symbol index on its own
make -C linux tags       # ctags only
make -C linux cscope     # cscope only
make -C linux clean      # also removes tags, cscope.* and bin/
```

`GPROF=1` needs the `clean` in front of it. It changes compiler flags rather than files, and make
decides what to rebuild from mtimes alone, so `make GPROF=1` over an existing tree reports success
and recompiles nothing; the binaries stay unprofiled. Going back the other way is the same trap in
reverse, where a plain `make` leaves every binary carrying `-pg`.

`tags` and `cscope.out` are file targets of the default build rather than a command to remember.
Left to a separate command they were never generated at all, and `<C-]>` answered `E433: No tags
file`. `tags` lists every `.c` and `.h` as a prerequisite and the source directories alongside
them; `cscope.out` reaches the same set through `cscope.files`, which carries those prerequisites.
The directories are in the list because deleting a file also removes the prerequisite that would
have reported the change, and only the directory's own mtime still moves. A build with nothing to
index still does nothing.

`-MMD -MP` has the compiler record what each object included, and the `-include` at the foot of the
Makefile feeds those lists back in. Without them a header edit rebuilt nothing: changing
`ADDR_TEXT_MAX_HELPER` in `net_util.h` left every binary byte-identical while make reported success.

`linux/.ctags.d/linux.ctags` holds the ctags settings, so an editor that invokes ctags
itself indexes the tree the same way `make tags` does. `--map-C=+.h` with `--map-C++=-.h` puts
headers on the C parser, since Universal Ctags maps `.h` to C++ by default. Measured on this tree
the two parsers agree on every tag name, so the pair currently changes nothing but the `language:`
field; it stays because that agreement is a property of what these headers happen to contain rather
than of the two grammars. `--extras=+q` gives qualified tags, so `zc_stats::copied` is reachable
rather than just `copied`. The result is 199 tags: functions, prototypes, macros with their
signatures,
struct and union members with their types. Naming the maps also replaces `--languages=C`, which
Universal Ctags 6.2.1 answers with a warning about its own TOML parser on every invocation.

`cscope -bkq` skips `/usr/include`, so a query answers about this tree instead of libc, and the
`u` in `-bkqu` forces a full rebuild. Left to itself cscope decides whether its database is current
by comparing whole-second mtimes, so an edit that lands in the same second as the previous build is
dropped without a word and the database answers from the older source. Make already tracks staleness
at nanosecond resolution; cscope should not overrule it with a coarser clock.

```sh
cd linux
cscope -dL -1 nl_recv_or_shutdown    # where is it defined
cscope -dL -3 wait_ready       # who calls it
```

### run

```sh
./linux/bin/tcp/echo_server &
./linux/bin/tcp/echo_client 127.0.0.1 "hello, socket"

# sendfile(2), port 5001
./linux/bin/zerocopy/sendfile_server linux/Makefile &
./linux/bin/zerocopy/sendfile_client 127.0.0.1

# splice(2), port 5000, drop-in replacement for tcp/echo_server
./linux/bin/zerocopy/splice_echo_server &
./linux/bin/tcp/echo_client 127.0.0.1 "hello, splice"

# MSG_ZEROCOPY sender into a TCP_ZEROCOPY_RECEIVE receiver, port 5002
./linux/bin/zerocopy/zerocopy_recv_server &
./linux/bin/zerocopy/msg_zerocopy_client 127.0.0.1

# netlink, read-only, no privileges needed
./linux/bin/netlink/link_dump
./linux/bin/netlink/addr_manage list
./linux/bin/netlink/sock_diag_dump
./linux/bin/netlink/genl_family_list
./linux/bin/netlink/route_monitor        # runs until SIGINT or SIGTERM

# netlink, changes the running system, needs CAP_NET_ADMIN -- use a throwaway VM
./linux/bin/netlink/addr_manage add lo 127.9.9.9/32
./linux/bin/netlink/addr_manage del lo 127.9.9.9/32
./linux/bin/netlink/nflog_listen 5
```

### layout

- `common/net_util.{c,h}` — BSD helpers. Every blocking step takes an optional signal mask:
  `tcp_listen_helper`, `tcp_connect_helper`, `tcp_accept_helper`, `udp_bind_helper`,
  `udp_connect_helper`, `wait_ready_or_shutdown_helper`, `recv_or_shutdown_helper`, `recvfrom_or_shutdown_helper`,
  `send_all_or_shutdown_helper`. `format_addr_helper` and `format_peer_helper` render a peer as
  `address:port`. `ignore_sigpipe_helper`, `register_signal_handler_helper`, `catch_shutdown_signals_helper`,
  `should_shut_down_helper` and `which_shutdown_signal_helper` cover the signal side. A wait ends for one of
  three reasons and
  each is told apart at the call site: readiness returns normally, a shutdown shows up as
  `should_shut_down_helper()`, and anything else leaves the kernel's own `errno` in place. That is what lets a
  server print `stopped by signal 15` and exit 0 while still reporting a real failure as one. Each
  helper takes only what a call site actually varies, which is why `tcp_listen_helper` is a port and a
  backlog and `SO_REUSEADDR` is set where it can be read rather than threaded in through a
  `setsockopt` triple no caller ever fills.
- `common/log.{c,h}` — every line the programs emit goes through one module, and the macro name says
  which stream it lands on. `LOG_MSG_HELPER` and `LOG_ERRNO_HELPER` write `program: function(): text` to stderr for
  diagnostics; `LOG_RESULT_HELPER` writes `program: text` to stdout for what the program was asked to
  produce. No call site repeats its own name and none of them call `printf` directly, so a pipeline
  still reads results with `link_dump | wc -l` while errors stay out of the data.
- Buffering is the log module's business, not each caller's. `stdio(3)` in the Linux man-pages says
  stdout is line buffered *when it points to a terminal*; redirected to a pipe or a file it is block
  buffered, and a newline no longer pushes anything out. Measured: a line printed to a pipe appeared
  1.00 s later, when the next write forced a flush, and vanished entirely when the process was killed
  first. `LOG_RESULT_HELPER` therefore flushes, once, in one place. `LOG_ERRNO_HELPER` needs no flush because
  stderr is unbuffered.
- `netlink/nl_util.{c,h}` — netlink helpers: `nl_open_helper`, `nl_join_group_helper`,
  `nl_send_helper`, `nl_recv_helper`, `nl_recv_or_shutdown_helper`, `nl_add_attr_helper`,
  `nl_parse_attrs_helper`, `nl_check_error_helper`, `nl_next_seq_helper`. It sits beside
  the programs that use it rather than in `common/`, because `netlink/` is its only consumer.
  `common/` is for code more than one topic uses; a helper is promoted there when a second caller
  appears, which is why iproute2 keeps `lib/libnetlink.c` in a library directory (`ip`, `ss`, `tc`
  and `bridge` all use it) while subsystem-local helpers stay in their subsystem. The Makefile's
  `TOPIC_LIB_SRCS` marks such a file as a library object so it is compiled but not linked as a
  program of its own.
- `tcp/` — `tcp_echo_server.c`, `tcp_echo_client.c`.
- `udp/` — `udp_echo_server.c`, `udp_echo_client.c`.
- `zerocopy/` — `sendfile_server.c`, `sendfile_client.c`, `splice_echo_server.c`,
  `msg_zerocopy_client.c`, `zerocopy_recv_server.c`.
- `netlink/` — `link_dump.c`, `route_monitor.c`, `addr_manage.c`, `sock_diag_dump.c`,
  `genl_family_list.c`, `nflog_listen.c`.
- `epoll/`, `unix_domain/` — reserved topics.

### zero-copy

Four kernel paths, one program each, ports 5000/5001/5002 so several can run side by side.

- `sendfile_server.c` — `sendfile(2)` pushes a file straight from the page cache to the socket.
  Takes the file path as its only argument. Under strace a 3 MiB file leaves as a single
  `sendfile()` call with no `read()` of the payload.
- `splice_echo_server.c` — `splice(2)` moves bytes socket → pipe → socket, so an echo never
  touches a user buffer. It listens on 5000 and answers `tcp/echo_client` unchanged.
- `msg_zerocopy_client.c` — `SO_ZEROCOPY` plus `send(MSG_ZEROCOPY)` hands the kernel the user
  pages instead of copying them. Each successful `send()` consumes one notification sequence
  number, read back from `MSG_ERRQUEUE` as an inclusive `[ee_info, ee_data]` range. The kernel
  coalesces ranges, so 16 sends usually arrive as fewer than 16 messages.
- `zerocopy_recv_server.c` — `mmap()` on the connected socket plus
  `getsockopt(TCP_ZEROCOPY_RECEIVE)` maps received pages into the process instead of copying
  them, with `madvise(MADV_DONTNEED)` releasing each batch.

Three kernel behaviours the examples have to accommodate:

- `TCP_ZEROCOPY_RECEIVE` never waits. An idle connection returns `length = 0` and
  `recv_skip_hint = 0`, which is indistinguishable from a stalled sender, so it cannot be read as
  end of stream. The real end of stream is the call failing with `EIO`. Everything the kernel
  declines to map is reported through `recv_skip_hint` and read with an ordinary `recv()`, which
  is also what blocks while the connection is idle.
- Only whole pages get mapped, so the mapped share depends on how the sender laid the data out.
  Against `msg_zerocopy_client` roughly 94% of 4 MiB arrives mapped; against a sender doing plain
  64 KiB `send()` calls it is 0% and everything falls back to `recv()`.
- Over loopback every `MSG_ZEROCOPY` notification comes back with `SO_EE_CODE_ZEROCOPY_COPIED`
  set: the receive path cannot keep pinned sender pages in another socket's queue, so it copies
  anyway. The flag is the point of the example — it is how a sender learns zero-copy did not
  happen.

`sendfile(2)` and `splice(2)` have no `MSG_NOSIGNAL`, so both servers ignore `SIGPIPE` and take
`EPIPE` from the return value instead. Without that a peer that resets mid-transfer kills the
process.

### netlink

A netlink socket is an ordinary `AF_NETLINK` socket whose peer is the kernel. The same
`send`/`recv` apply; what changes is that a request can answer with a multipart dump terminated by
`NLMSG_DONE`, and that a socket can also subscribe to multicast groups and receive messages nobody
asked for. Each program here is modelled on a job some real tool already does over netlink, not on
a feature list.

- `link_dump.c` — dump the interface list with `RTM_GETLINK` and walk the attributes. This is the
  question a capture tool asks before it can do anything else; `pcap_findalldevs` is the same
  query, and `ifi_type` is what decides which link-layer header the caller will have to parse.
- `route_monitor.c` — subscribe to `RTNLGRP_LINK`, the address groups and `RTNLGRP_IPV4_ROUTE`,
  then print events as they happen. A capture or firewall daemon that runs for days needs this to
  notice that its interface went down or that an address moved. It is also where both halves of the
  signal question live: `SIGINT`/`SIGTERM` interrupt the wait and end the loop, while `SIGUSR1`
  reports the event counters with `SA_RESTART` set because reporting must not end anything.
- `sock_diag_dump.c` — `SOCK_DIAG_BY_FAMILY` with `inet_diag_req_v2`, which is what `ss` runs.
  Needs `CONFIG_INET_DIAG`; without it the dump fails with `ENOENT` rather than the socket refusing
  to open.
- `genl_family_list.c` — `CTRL_CMD_GETFAMILY` against generic netlink. Every ethtool, nl80211 or
  devlink client starts by resolving a family name to a runtime id, because generic netlink ids are
  not fixed. `CTRL_ATTR_MCAST_GROUPS` is nested, so it also exercises attributes inside attributes.
- `addr_manage.c` — the write direction: `RTM_NEWADDR` and `RTM_DELADDR` with `NLM_F_ACK`, and the
  `NLMSG_ERROR` reply turned back into `errno`. This one changes the running system.
- `nflog_listen.c` — bind an NFLOG group over `NETLINK_NETFILTER` and read packets the ruleset
  already selected, which is the path `ulogd` takes instead of an `AF_PACKET` capture socket. The
  kernel does the filtering; the program only reads what survived it.

Details the protocol forces on the caller, all of them in `netlink/nl_util.c`:

- Netlink is message-oriented, so a short `sendto` cannot be retried from the offset the way
  `send_all_or_shutdown_helper` retries a stream. A partial write is reported as `EMSGSIZE` instead.
- `recvfrom` uses `MSG_TRUNC`, which returns the real message length even when it did not fit, so
  an oversized message is caught rather than silently parsed from a truncated buffer.
- The sender is checked: any local process can address a netlink socket if it knows the port id, so
  a message whose `nl_pid` is not 0 is rejected. Binding leaves `nl_pid` at 0 as well, letting the
  kernel assign a unique port id; hardcoding `getpid()` breaks the moment a second netlink socket
  exists in the process.
- `NLMSG_OK`, `NLMSG_NEXT`, `RTA_OK` and `RTA_NEXT` decrement the length they are given, and the
  last attribute can be shorter than its own alignment. The length variable has to be `int`; a
  `size_t` wraps to a huge value at the tail and the walk runs off the buffer.
- Attribute types carry `NLA_F_NESTED` in their high bits, so `nl_parse_attrs_helper` masks with
  `NLA_TYPE_MASK` before indexing. Without the mask every nested attribute is skipped.
- A dump that raced with a change comes back with `NLM_F_DUMP_INTR` set and is inconsistent. The
  dump programs report it instead of printing a half-truth.
- Netlink defines no timeout, so none is invented here. `nl_recv_or_shutdown_helper` waits until the kernel
  answers or a shutdown signal arrives. A dump ends on `NLMSG_DONE` and an `NLM_F_ACK` exchange on its
  `NLMSG_ERROR`; both are the kernel's own terminators, and waiting for them is the honest behaviour
  when nothing in `netlink(7)` promises a deadline.
- A dump request for links has to carry `IFLA_EXT_MASK`. Without it the kernel never sizes the dump
  buffer per device, and an interface whose `RTM_NEWLINK` message does not fit is left out of the
  answer with no error anywhere. A netns holding one device with 400 long altnames reproduces it:
  the dump silently returns one interface fewer than `ip link` lists.
- `NLMSG_DONE` carries the dump's exit status in its payload. A dump the kernel abandoned part-way
  still ends with `NLMSG_DONE`, so treating the message itself as success reports a truncated result
  as a complete one. `nl_check_done_helper` reads that `int` and turns it back into `errno`.
- `ENOBUFS` on a multicast socket means the kernel dropped events this reader was too slow to take.
  It is not a broken socket: the right response is to record the gap and keep reading, which is why
  `route_monitor` counts overruns instead of exiting, and asks for a larger `SO_RCVBUF` up front.
- The read buffer bounds what a single message may be. `nflog_listen` therefore asks the kernel for
  a copy range that fits in it; requesting 64 KiB of every packet into a smaller buffer turns the
  first large packet into `EMSGSIZE`. `nl_recv_helper` reports that rather than parsing a truncated
  datagram, and production code that cannot cap the size resizes with `MSG_PEEK | MSG_TRUNC`.

`addr_manage add`/`del` and `nflog_listen` are the two that touch kernel state, so both carry the
warning at the top of the file and were verified under QEMU rather than on the host. The rest only
read and were checked against `ip`, `ss` and `genl` on the host: identical interface, address and
socket lists, and the same 26 generic netlink families.

### profiling & tracing

The always-on flags (`-ggdb3`, frame pointers, no sibling-call optimization, async unwind
tables) keep the binaries debuggable and traceable under perf, strace, gdb, valgrind, and
uftrace with no separate build (`uftrace record -P. ./linux/bin/tcp/echo_client`). `-pg` for gprof /
mcount-based uftrace is opt-in via `make -C linux GPROF=1`.

## Winsock/

Native Winsock, developed on a Windows machine in Visual Studio 2026. It cannot be built or
validated from the Linux side of this repo, so nothing here is cross-compiled or stubbed.

```
Winsock/
|-- Winsock.slnx                     solution, x64 and x86
|-- Common/NetUtils/                 NetStartup, ListenTcp, ConnectTcp, BindUdp, ConnectUdp,
|                                    SendAll, SetNonBlocking, FormatAddr, FormatPeer,
|                                    LOG_MSG, NET_PERROR, LOG_RESULT
|-- Tcp/TcpEchoServer/
`-- Tcp/TcpEchoClient/
```

The two trees answer the same questions with each platform's own vocabulary rather than a shared
abstraction: `tcp_listen_helper` against `ListenTcp`, `LOG_ERRNO_HELPER` against `NET_PERROR`, `format_peer_helper`
against `FormatPeer`, `errno` against `WSAGetLastError`. Both echo servers name the peer they read
from rather than echoing in silence, and both split results from diagnostics the same way. Style is
enforced per tree by the root `.editorconfig` (`[Winsock/**]` section) and `Winsock/.clang-format`
(Allman braces, forced braces on one-line bodies, left-aligned pointers). `SortIncludes` stays off
there because `<winsock2.h>` must precede `<windows.h>`.
