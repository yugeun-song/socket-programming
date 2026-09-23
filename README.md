# socket-programming

Socket programming study with the Windows and Linux ecosystems kept fully separate. There is no
cross-platform abstraction layer: each tree is native, and naming, brace style, line endings and
build system all differ by platform.

## Layout

```
socket-programming/
|-- .editorconfig     root config; branches EOL and indentation by path
|-- .gitattributes    linux/** = LF, Winsock/** = CRLF
|-- .gitignore        single root ignore file
|-- linux/            native BSD sockets  (snake_case, K&R, LF, make)
`-- Winsock/          native Winsock      (PascalCase, Allman, CRLF, VS 2026)
```

Conventions are enforced per tree because the two toolchains differ:

- `.editorconfig` lives at the **root** and branches by path section: `[Winsock/**]` switches to
  CRLF, `[**/Makefile]` to hard tabs, and `insert_final_newline = false` holds everywhere, Winsock
  included. `root = true` stops the upward walk, so one file drives both trees.
- `.clang-format` is **per tree**, because clang-format takes the nearest config walking upward and
  cannot branch by path within one file: `linux/` is LLVM / K&R braces / `int *p`, `Winsock/` is
  Microsoft / Allman / `int* p`.
- `IncludeBlocks: Regroup` orders the `linux/` includes rather than any hand discipline: own header,
  flat `<...>`, `<sys/...>`, `<arpa|net|netinet/...>`, `<linux/...>`, project `"..."`, alphabetical
  within a group and one blank line between. Own header first proves it compiles alone, and the
  groups widen from portable to platform-specific; `_GNU_SOURCE` and its kind stay above the block
  because they decide what the headers below expose.
- `Winsock/.clang-format` keeps `SortIncludes: false` as a guard, not a fix. Nothing there needs it
  today — `<winsock2.h>` and `<ws2tcpip.h>` already sort in order and `WIN32_LEAN_AND_MEAN` handles
  the classic clash — but `<windows.h>` sorts above `<winsock2.h>`.
- Braces hold their contents at arm's length: `Cpp11BracedListStyle: false` gives `{ 0 }` over `{0}`,
  `SpaceInEmptyBraces: Always` gives `{ }`, and a list too long for the line wraps inside the braces
  with the spaces intact. `SpaceInEmptyBraces` needs clang-format 21; the older `SpaceInEmptyBlock`
  is still accepted but does nothing.
- 120 columns, stated twice because two tools have to agree: `ColumnLimit` per `.clang-format` and
  `max_line_length` in `.editorconfig`. `ColumnLimit` alone is weighed against
  `PenaltyExcessCharacter`, once set to 1 here, so overflow beat breaking and nine lines across seven
  files ran past 120 while clang-format still reported the tree clean. That key is gone now.
- `PenaltyBreakAssignment: 100` wraps a long call inside its argument list rather than after the `=`;
  the default of 2 strands the callee on its own line and hides which variable is written.

Rules no formatter can check, applied to every source in both trees:

- Prefix `++i`, never `i++`, and no assignment or `++`/`--` inside a larger expression: never
  `foo(i++)`, `buf[i++]`, `*p++`, or an assignment in a condition. Testing a call's return in place,
  `if (listen(fd, backlog) < 0)`, is the one exception, taken 114 times.
- Locals are declared in one block at the top of the function body, and only side-effect-free
  initializers stay on the declaration. One declaration per name per function, no shadowing.
- Header prototypes are packed by concern with a blank line between groups, never one blank line per
  declaration. The grouping is what carries the information.
- Macro names are upper case, always. The case is the only warning that `LOG_MSG_HELPER` and
  `LOG_ERRNO_HELPER` inject `__func__` out of nowhere, and that `NLMSG_OK` and `RTA_NEXT` evaluate an
  argument more than once.
- A variable is named for what it counted: `received_bytes`, `sent_bytes`, `moved_bytes`,
  `received_total`, `poll_status`, `remaining`, `report_signal_number`, `drain_status`. `n` says only
  that a number arrived.
- Short is right only for the name everyone already knows: `fd`, `len`, `i`. `nlh` became
  `nl_header`, `tb` became `attr_table`, `zc` became `zerocopy`, and `peer` — which reads like a
  descriptor and held none — became `log_buf`, an address rendered for a log line.
- A name that leaves a question open answers it. These waits end on readiness or on a shutdown signal
  and nothing else: `wait_ready_or_shutdown_helper`, `recv_or_shutdown_helper`,
  `recvfrom_or_shutdown_helper`, `send_all_or_shutdown_helper`, `nl_recv_or_shutdown_helper`. The
  earlier `_until` suffix named a clock they no longer watch.
- A wrapper keeps the name of the call it wraps: `tcp_listen_helper` for `listen(2)`, returning the
  `server_fd` that pairs with `tcp_accept_helper`'s `client_fd`. `create_server` would hide the
  syscall the example exists to show.
- A buffer is named for its direction — `recv_buf`, `send_buf`, `recvBuf`, `sendBuf` — and an echo
  server keeps `recv_buf` for the bytes it sends back, because the name says how they got there.
  `error_buf` is scratch for `strerror_r`. A yes-or-no says so: `is_done`, `is_listing`,
  `has_failed`.
- Branches of one decision are one chain. Two `if`s in a row testing the same value are a chain that
  lost its `else`: negative, zero and positive are three outcomes of a single read. A guard that
  leaves early with its own cleanup stays a separate `if`, and two arms ending in the same `break`
  share it.
- `errno` carries what the kernel reported and nothing else. `ETIMEDOUT` on a TCP socket means TCP
  gave up on the connection, so borrowing it for an expired wait of our own made the two arrive
  identically and left the real one unhandled. Our outcomes travel in the return value, and
  `should_shut_down_helper()` answers whether a shutdown was asked for.
- Whoever receives says who sent: every program that reads from a socket prints the peer as
  `address:port`. The netlink programs are exempt, their peer being the kernel.

Everything under `linux/` is written as if it were one thread among many and as if signals arrive at
any moment, even where the program is single threaded and installs no handler:

- Descriptors are created close-on-exec atomically — `SOCK_CLOEXEC`, `O_CLOEXEC`, `pipe2`, `accept4`
  — since setting `FD_CLOEXEC` afterwards leaves a window in which another thread's `fork`/`exec`
  inherits the descriptor.
- Every blocking call handles `EINTR`. `tcp_accept_helper` and `nl_send_helper` retry internally;
  loops that need to notice a shutdown request let `EINTR` reach them and re-test their flag.
- `sigaction`, never `signal`, whose behaviour carries historical SysV/BSD differences and which
  states neither `sa_mask` nor `SA_RESTART`. `register_signal_handler_helper` takes the flag as an
  argument, because the choice belongs to the signal's purpose and not to a house default.
- A handler stores the signal number it was given into a `volatile sig_atomic_t` and does nothing
  else, so `which_shutdown_signal_helper()` can say whether `SIGINT` or `SIGTERM` ended the run, with
  no `(void)` cast and no unused attribute. Doing more is the hazard: the handler runs on the
  interrupted thread and shares its `errno`, and one `close()` in a handler turned a waiting `ERANGE`
  into `EBADF` before the interrupted line ever looked at it.
- `pthread_sigmask` does not report failure the way the rest of the socket API does. `sigprocmask(2)`
  returns -1 and sets `errno`; `pthread_sigmask(3)` returns the error number itself and leaves
  `errno` alone, so `if (pthread_sigmask(...) < 0)` reads `EINVAL` as success. Both callers assign
  the result to `errno` first, then `catch_shutdown_signals_helper` returns -1 as everything else in
  the tree does and `route_monitor` exits 1.
- Which flag a handler sets decides whether `SA_RESTART` belongs: `route_monitor` installs `SIGINT`
  and `SIGTERM` bare because a shutdown must interrupt the wait, gives its `SIGUSR1` counter report
  `SA_RESTART` because reporting must not end anything, and `SIG_IGN` takes neither. The flag never
  covers `poll`, `select`, `epoll_wait`, `ppoll` or a socket carrying `SO_RCVTIMEO`, so
  `wait_ready_or_shutdown_helper` sees `EINTR` regardless and the flag only documents intent there.
- **A timeout belongs to whoever defines one.** TCP does: the kernel retransmits, gives up in
  `tcp_write_err()`, and hands the next `recv`, `send` or `connect` an `ETIMEDOUT`, which
  `TCP_USER_TIMEOUT` only reschedules, so recognising `ETIMEDOUT` is the whole of the handling. UDP
  and netlink define none of their own: `udp(7)` and `netlink(7)` mention no timeout and neither
  `net/ipv4/udp.c` nor `net/netlink/` contains `ETIMEDOUT`. Nothing here runs a clock of its own.
- Testing a shutdown flag and then blocking loses a signal that lands between the two steps, so
  `wait_ready_or_shutdown_helper` calls `ppoll`: the long-running programs block the signals they
  handle before entering the loop and hand `ppoll` the original mask, making the unblock and the wait
  one atomic step. Blocking is not discarding — a blocked `SIGTERM` sits in the pending set, visible
  as `SigPnd` in `/proc/self/status`, until that swap runs the handler. `SIGPIPE` is the only signal
  genuinely `SIG_IGN`, trading a process kill for an `EPIPE` the write path reports; `send` also
  passes `MSG_NOSIGNAL`, which `splice` and `sendfile` have no equivalent of.
- **Waiting happens in `ppoll`.** Every AF_INET socket is created `SOCK_NONBLOCK` and `splice` passes
  `SPLICE_F_NONBLOCK`, so `recv`, `send`, `accept4` and `splice` return at once; the netlink socket
  is the exception, left blocking and read only behind `nl_recv_or_shutdown_helper`, which polls
  first. A poll in front of a blocking call bounds nothing — it answers "at least one byte fits", and
  the call then writes megabytes and sleeps inside the kernel. Measured against a receiver that never
  read, a blocking `sendfile()` moved 2.6 MB and then stayed in the kernel as long as the peer was
  up; non-blocking it returns in 0.000207 s and the loop sees `EAGAIN`.
- No mutable globals beyond two signal flags, the shutdown flag in `common/net_util.c` and
  `route_monitor`'s `SIGUSR1` report flag; a shared counter takes a mutex, with `nl_next_seq_helper`
  handing out netlink sequence numbers under `PTHREAD_MUTEX_INITIALIZER`. The tree builds as
  `-std=c99`, which predates `<stdatomic.h>`, and an uncontended glibc mutex is a userspace
  compare-exchange with no `futex` call: 2.27 ns against 9.50 ns for a `pthread_spinlock_t`.
- Only thread-safe library calls: `strerror_r` not `strerror`, `inet_ntop` not `inet_ntoa`,
  `if_indextoname` into a caller buffer.
- `common/log.c` formats a whole line into a local buffer and emits it with one `fprintf`; three
  stdio calls per message would let concurrent threads interleave mid-line.

## linux/

TCP and UDP echo, four zero-copy transfer paths, and six netlink programs, built with a Makefile.
Every server that accepts a connection handles one and exits. The UDP echo server has no connection
to end, so it serves datagrams until a signal stops it.

### build

```sh
make -C linux                 # binaries at linux/bin/<topic>/<name>, plus tags, cscope.out and compile_commands.json
make -C linux clean && make -C linux GPROF=1    # -pg for gprof / mcount-based uftrace
make -C linux symbols         # the symbol index on its own
make -C linux tags            # ctags only
make -C linux cscope          # cscope only
make -C linux compdb          # compile_commands.json only
make -C linux format          # clang-format -i over every source and header
make -C linux format-check    # clang-format --dry-run --Werror; nonzero on drift
make -C linux clean           # also removes tags, cscope.*, compile_commands.json and bin/
```

`GPROF=1` needs the `clean` in front of it: it changes compiler flags rather than files, and make
decides what to rebuild from mtimes alone, so over an existing tree it reports success and recompiles
nothing. The reverse is the same trap, a plain `make` leaving every binary carrying `-pg`.

`tags`, `cscope.out` and `compile_commands.json` are file targets of the default build rather than a
command to remember; left to a separate command they went ungenerated until `<C-]>` answered
`E433: No tags file`. All three depend on every `.c` and `.h` plus the source directories,
`cscope.out` reaching that set through `cscope.files`; the directories are listed because deleting a
file also removes the prerequisite that would have reported the change.

`-MMD -MP` has the compiler record what each object included, and the `-include` at the foot of the
Makefile feeds those lists back in. Without them a header edit rebuilt nothing: changing
`ADDR_TEXT_MAX_HELPER` in `net_util.h` left every binary byte-identical while make reported success.

`linux/.ctags.d/linux.ctags` holds the ctags settings, so an editor invoking ctags itself indexes the
tree the way `make tags` does. `--map-C=+.h` with `--map-C++=-.h` puts headers on the C parser, which
Universal Ctags otherwise maps to C++; measured here both parsers produce the same tag names, so the
pair changes only the `language:` field. `--extras=+q` makes `zc_stats::copied` reachable rather than
just `copied`. The result is 199 tags, prototypes, macro signatures and struct members included.
Naming the maps also replaces `--languages=C`, which Universal Ctags 6.2.1 answers with a
TOML-parser warning on every invocation.

`cscope -bkq` skips `/usr/include`, so a query answers about this tree instead of libc, and the `u`
in `-bkqu` forces a full rebuild. Left to itself cscope compares whole-second mtimes, so an edit
landing in the same second as the previous build is dropped without a word.

```sh
cd linux
cscope -dL -1 nl_recv_or_shutdown_helper     # where is it defined
cscope -dL -3 wait_ready_or_shutdown_helper  # who calls it
```

`compile_commands.json` comes from the Makefile rather than from `bear`, so it tracks the flags above
without a second tool. It has to reproduce the build exactly: `-std=c99` defines `__STRICT_ANSI__`,
which hides `sigset_t`, so every executable defines `_POSIX_C_SOURCE 200809L` before its first
include, and library objects reach the same place through their own `#define _GNU_SOURCE`. Before
that macro the executable rule compiled only because `$(LDFLAGS)` put `-pthread` on the same
invocation, whose `_REENTRANT` glibc's `features.h` maps to `_POSIX_C_SOURCE 199506L` (not
`_DEFAULT_SOURCE`); without `-pthread` on the executable entries clangd answered with 316 errors
over the tree.

`linux/.clangd` hands `.h` files `-pthread`. A header opened on its own has no entry, so clangd borrows
one from a file that includes it; `net_util.h` drew `net_util.c`, which carries no `-pthread`, and
reported six `unknown type name 'sigset_t'`. `-pthread` rather than `-D_GNU_SOURCE` because it is the
thinnest environment any includer supplies: `_GNU_SOURCE` opens `__USE_GNU` and 19 more `__USE_*`, so a
header could declare a GNU-only call, read clean on its own, and still break `tcp_echo_server.c`. The
same file drops `UnusedIncludes`, because clangd reads `sigset_t` as coming from
`bits/types/__sigset_t.h` and calls the `<signal.h>` that supplies it unused, and suppresses the five
`unused-*` diagnostics: a handler installed through `sigaction` carries the signature the API fixes,
`(int, siginfo_t *, void *)` under `SA_SIGINFO`, and one that only raises a flag reads none of it.
The build still reports them.

### run

```sh
./linux/bin/tcp/tcp_echo_server &
./linux/bin/tcp/tcp_echo_client 127.0.0.1 "hello, socket"

# UDP, port 5000, until SIGINT or SIGTERM
./linux/bin/udp/udp_echo_server &
./linux/bin/udp/udp_echo_client 127.0.0.1 "hello, udp"

# sendfile(2), port 5001
./linux/bin/zerocopy/sendfile_server linux/Makefile &
./linux/bin/zerocopy/sendfile_client 127.0.0.1

# splice(2), port 5000, drop-in for tcp/tcp_echo_server
./linux/bin/zerocopy/splice_echo_server &
./linux/bin/tcp/tcp_echo_client 127.0.0.1 "hello, splice"

# MSG_ZEROCOPY into TCP_ZEROCOPY_RECEIVE, port 5002
./linux/bin/zerocopy/zerocopy_recv_server &
./linux/bin/zerocopy/msg_zerocopy_client 127.0.0.1

# netlink, read-only, unprivileged
./linux/bin/netlink/link_dump
./linux/bin/netlink/addr_manage list
./linux/bin/netlink/sock_diag_dump
./linux/bin/netlink/genl_family_list
./linux/bin/netlink/route_monitor        # until SIGINT or SIGTERM

# netlink, changes the running system, needs CAP_NET_ADMIN -- use a throwaway VM
./linux/bin/netlink/addr_manage add lo 127.9.9.9/32
./linux/bin/netlink/addr_manage del lo 127.9.9.9/32
./linux/bin/netlink/nflog_listen 5       # needs an NFLOG rule feeding group 5
```

The tcp, udp and splice servers all bind 5000, so those stanzas run one at a time.

### layout

- `common/net_util.{c,h}` — BSD helpers. The six that wait take an optional signal mask:
  `tcp_accept_helper`, `tcp_connect_helper` (on `POLLOUT`, for the non-blocking connect),
  `wait_ready_or_shutdown_helper`, `recv_or_shutdown_helper`, `recvfrom_or_shutdown_helper`,
  `send_all_or_shutdown_helper`. `tcp_listen_helper`, `udp_bind_helper` and `udp_connect_helper`
  take none. `format_addr_helper` and
  `format_peer_helper` render a peer as `address:port`; the signal side is `ignore_sigpipe_helper`,
  `register_signal_handler_helper`, `catch_shutdown_signals_helper`, `should_shut_down_helper` and
  `which_shutdown_signal_helper`. A wait ends three ways, told apart at the call site: readiness
  returns normally, a shutdown shows up as `should_shut_down_helper()`, anything else leaves the
  kernel's own `errno` in place. That is what lets a server print `stopped by signal 15` and exit 0
  while still reporting a real failure as one. `tcp_listen_helper` takes only a port and a backlog,
  and sets `SO_REUSEADDR` where it can be read.
- `common/log.{c,h}` — every emitted line goes through one module and the macro name says which
  stream it lands on: `LOG_MSG_HELPER` and `LOG_ERRNO_HELPER` write `program: function(): text` to
  stderr, `LOG_RESULT_HELPER` writes `program: text` to stdout. No call site repeats its own name or
  calls `printf`, so `link_dump | wc -l` still reads results while errors stay out of the data.
- Buffering is the log module's business. `stdio(3)` line buffers stdout only when it points to a
  terminal; on a pipe or a file it is block buffered and a newline pushes nothing out. Measured: a
  line printed to a pipe appeared 1.00 s late, at the flush, and vanished entirely when the process
  was killed first. `LOG_RESULT_HELPER` therefore flushes once, in one place; stderr is unbuffered,
  so `LOG_ERRNO_HELPER` does not.
- `netlink/nl_util.{c,h}` — `nl_open_helper`, `nl_join_group_helper`, `nl_set_rcvbuf_helper`,
  `nl_send_helper`, `nl_recv_helper`, `nl_recv_or_shutdown_helper`, `nl_add_attr_helper`,
  `nl_parse_attrs_helper`, `nl_check_error_helper`, `nl_check_done_helper`, `nl_next_seq_helper`. It
  sits beside its only consumer rather than in `common/`, which is for code more than one topic uses;
  a helper moves there on the second caller, the way iproute2 keeps `lib/libnetlink.c` in a library
  directory for `ip`, `ss`, `tc` and `bridge`. The Makefile's `TOPIC_LIB_SRCS` marks such a file as a
  library object: compiled, not linked as a program of its own.
- `tcp/` — `tcp_echo_server.c`, `tcp_echo_client.c`. `udp/` — `udp_echo_server.c`,
  `udp_echo_client.c`.
- `zerocopy/` — `sendfile_server.c`, `sendfile_client.c`, `splice_echo_server.c`,
  `msg_zerocopy_client.c`, `zerocopy_recv_server.c`.
- `netlink/` — `link_dump.c`, `route_monitor.c`, `addr_manage.c`, `sock_diag_dump.c`,
  `genl_family_list.c`, `nflog_listen.c`.
- `epoll/`, `unix_domain/` — reserved topics, named in the Makefile's `SRC_DIRS` with no directory
  yet.

### zero-copy

Four kernel paths across five programs, on ports 5000/5001/5002 so several can run side by side.

- `sendfile_server.c` — `sendfile(2)` pushes a file, its only argument, from the page cache to the
  socket. Under strace 3 MiB leaves as a single `sendfile()` with no `read()` of the payload;
  `sendfile_client.c` drains it with an ordinary `recv()`.
- `splice_echo_server.c` — `splice(2)` socket → pipe → socket, so an echo never touches a user
  buffer. Port 5000, and `tcp/tcp_echo_client` works against it unchanged.
- `msg_zerocopy_client.c` — `SO_ZEROCOPY` plus `send(MSG_ZEROCOPY)` hands the kernel the user pages.
  One notification sequence number per successful `send()`, read from `MSG_ERRQUEUE` as an inclusive
  `[ee_info, ee_data]` range; the kernel coalesces ranges, so fewer messages come back than sends
  went out.
- `zerocopy_recv_server.c` — `mmap()` on the connected socket plus
  `getsockopt(TCP_ZEROCOPY_RECEIVE)` maps received pages in, and `madvise(MADV_DONTNEED)` releases
  each batch.

Three kernel behaviours to accommodate:

- `TCP_ZEROCOPY_RECEIVE` never waits. Idle gives `length = 0` and `recv_skip_hint = 0`,
  indistinguishable from a stalled sender, so it cannot be read as end of stream; that is the call
  failing with `EIO`. The wait comes from the `ppoll` at the top of the loop, before each
  `getsockopt`, and whatever the kernel declines to map comes back through `recv_skip_hint` for an
  ordinary `recv()`.
- Only whole pages get mapped, so the share follows the sender's layout: roughly 94% of 4 MiB from
  `msg_zerocopy_client`, 0% from a sender doing plain 64 KiB `send()` calls.
- Over loopback every `MSG_ZEROCOPY` notification comes back with `SO_EE_CODE_ZEROCOPY_COPIED` set,
  because the receive path cannot keep pinned sender pages in another socket's queue. The flag is the
  point of the example: it is how a sender learns zero-copy did not happen.

Neither `sendfile(2)` nor `splice(2)` has a `MSG_NOSIGNAL`, so both servers ignore `SIGPIPE` and take
`EPIPE` from the return value; without that a peer resetting mid-transfer kills the process.

### netlink

A netlink socket is an ordinary `AF_NETLINK` socket whose peer is the kernel. `send`/`recv` are
unchanged; new are the multipart dump terminated by `NLMSG_DONE` and the multicast groups that
deliver messages nobody asked for. Each program is modelled on a job some real tool already does.

- `link_dump.c` — `RTM_GETLINK` plus an attribute walk, the query `pcap_findalldevs` makes;
  `ifi_type` decides which link-layer header the caller has to parse.
- `route_monitor.c` — subscribe to `RTNLGRP_LINK`, the address groups and `RTNLGRP_IPV4_ROUTE` and
  print events live, which a capture or firewall daemon needs to notice that its interface went down
  or an address moved. `SIGINT`/`SIGTERM` interrupt the wait and end the loop; `SIGUSR1` reports the
  counters with `SA_RESTART`, because reporting must not end anything.
- `sock_diag_dump.c` — `SOCK_DIAG_BY_FAMILY` with `inet_diag_req_v2`, what `ss` runs. Needs
  `CONFIG_INET_DIAG`: without it the socket still opens and the dump fails with `ENOENT`.
- `genl_family_list.c` — `CTRL_CMD_GETFAMILY`. Generic netlink ids are not fixed, so every ethtool,
  nl80211 or devlink client resolves a family name to a runtime id first, and
  `CTRL_ATTR_MCAST_GROUPS` is nested, exercising attributes inside attributes.
- `addr_manage.c` — the write direction: `RTM_NEWADDR` and `RTM_DELADDR` with `NLM_F_ACK`, the
  `NLMSG_ERROR` reply turned back into `errno`.
- `nflog_listen.c` — bind an NFLOG group over `NETLINK_NETFILTER` and read packets the ruleset
  already selected, the path `ulogd` takes instead of an `AF_PACKET` capture socket.

Details the protocol forces on the caller. `netlink/nl_util.c` absorbs the ones every program
repeats; the ones tied to a single request stay in the program that makes it:

- Netlink is message-oriented, so a short `sendto` cannot resume from the offset the way
  `send_all_or_shutdown_helper` retries a stream; a partial write becomes `EMSGSIZE`. `recvfrom` uses
  `MSG_TRUNC`, which reports the real message length even when it did not fit, so an oversized
  message is caught instead of parsed from a truncated buffer.
- A message whose `nl_pid` is not 0 is rejected, since any local process knowing the port id can
  address the socket. Binding leaves `nl_pid` at 0 so the kernel assigns a unique one; `getpid()`
  breaks the moment a second netlink socket exists in the process.
- `NLMSG_OK`, `NLMSG_NEXT`, `RTA_OK` and `RTA_NEXT` decrement the length they are given, and the last
  attribute can be shorter than its own alignment, so that length has to be `int`: a `size_t` wraps
  at the tail and the walk runs off the buffer.
- `nl_parse_attrs_helper` masks with `NLA_TYPE_MASK` before indexing, since `NLA_F_NESTED` sits in
  the type's high bits; without the mask every nested attribute is skipped.
- A dump raced by a change comes back with `NLM_F_DUMP_INTR` and is inconsistent. `link_dump`,
  `sock_diag_dump` and `genl_family_list` report it instead of printing a half-truth; `addr_manage
  list` dumps too and skips the check.
- Netlink defines no timeout, so none is invented. `nl_recv_or_shutdown_helper` waits for the kernel
  or a shutdown signal, and one of the kernel's own terminators ends it: `NLMSG_DONE` for a dump,
  `NLMSG_ERROR` for an `NLM_F_ACK` exchange.
- A link dump has to carry `IFLA_EXT_MASK`, which `link_dump` adds itself. Without it the kernel
  never sizes the dump buffer per device, and an interface whose `RTM_NEWLINK` does not fit is left
  out with no error anywhere. A netns holding one device with 400 long altnames reproduces it: one
  interface fewer than `ip link` lists.
- `NLMSG_DONE` carries the dump's exit status in its payload, and an abandoned dump still ends with
  it, so `nl_check_done_helper` reads that `int` back into `errno` rather than trusting the message.
- `ENOBUFS` on a multicast socket means dropped events, not a broken socket: `route_monitor` records
  the gap, keeps reading, and asks for 1 MiB up front through `SO_RCVBUFFORCE`, falling back to
  `SO_RCVBUF`.
- The read buffer bounds one message, so `nflog_listen` asks for a 4 KiB copy range that fits it;
  64 KiB of every packet into a smaller buffer turns the first large packet into `EMSGSIZE`, which
  `nl_recv_helper` reports rather than parsing a truncated datagram. Code that cannot cap the size
  resizes with `MSG_PEEK | MSG_TRUNC`.

`addr_manage add`/`del` and `nflog_listen` are the two that touch kernel state, so both carry the
warning at the top of the file and were verified under QEMU. The rest only read and matched `ip`,
`ss` and `genl` on the host: same interface, address and socket lists, same 26 generic netlink
families.

### profiling & tracing

The always-on flags (`-ggdb3`, frame pointers, no sibling-call optimization, async unwind tables)
keep the binaries debuggable and traceable under perf, gdb, valgrind, uftrace and `strace -k` with no
separate build (`uftrace record -P. ./linux/bin/tcp/tcp_echo_client`). `make -C linux GPROF=1` adds
`-pg` to both `CFLAGS` and `LDFLAGS` for gprof and mcount-based uftrace.

## Winsock/

Native Winsock, developed on a Windows machine in Visual Studio 2026. It cannot be built or validated
from the Linux side of this repo, so nothing here is cross-compiled or stubbed.

```
Winsock/
|-- Winsock.slnx                     solution, x64 and x86
|-- Common/NetUtils/                 NetStartupHelper, NetCleanupHelper, ListenTcpHelper,
|                                    ConnectTcpHelper, BindUdpHelper, ConnectUdpHelper,
|                                    SendAllHelper, FormatAddrHelper, FormatPeerHelper,
|                                    LOG_MSG_HELPER, NET_PERROR_HELPER, LOG_RESULT_HELPER
|-- Tcp/TcpEchoServer/
`-- Tcp/TcpEchoClient/
```

Per-platform vocabulary, no shared abstraction: `tcp_listen_helper` against `ListenTcpHelper`,
`LOG_ERRNO_HELPER` against `NET_PERROR_HELPER`, `format_peer_helper` against `FormatPeerHelper`,
`errno` against `WSAGetLastError`. Both echo servers name the peer they read from, and both split
stdout results from stderr diagnostics. Style comes from the root `.editorconfig` (`[Winsock/**]`:
CRLF) and `Winsock/.clang-format` (Microsoft base, Allman braces, forced braces on one-line bodies,
left-aligned pointers, `SortIncludes` off).