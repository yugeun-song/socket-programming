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
- Whoever receives says who sent. Every program that reads from a socket prints the peer as
  `address:port`, so a reply is evidence of a specific exchange rather than a hopeful echo. The
  netlink programs are exempt: their peer is always the kernel.

Everything under `linux/` is written as if it were one thread among many and as if signals arrive
at any moment, even where the program itself is single threaded and installs no handler:

- Descriptors are created close-on-exec atomically: `SOCK_CLOEXEC`, `O_CLOEXEC`, `pipe2`, `accept4`.
  Opening first and setting `FD_CLOEXEC` afterwards leaves a window in which another thread's
  `fork`/`exec` inherits the descriptor.
- Every blocking call handles `EINTR`. `tcp_accept` and `nl_send` retry internally; loops that need
  to notice a shutdown request let `EINTR` reach them and re-test their stop flag.
- `sigaction`, never `signal`. `signal` carries historical SysV/BSD differences, and `sigaction`
  is where `sa_mask` and `SA_RESTART` can be stated explicitly. `install_signal_handler` takes the
  flag as an argument named at the call site — `SIGNAL_INTERRUPTS` or `SIGNAL_RESTARTS` — because
  the choice belongs to the signal's purpose and not to a house default.
- A handler assigns to a `volatile sig_atomic_t` flag and does nothing else. Which flag it is decides
  whether `SA_RESTART` belongs: a stop signal must interrupt the wait, so `route_monitor` installs
  `SIGINT` and `SIGTERM` with `SIGNAL_INTERRUPTS`; its `SIGUSR1` counter report must not tear the
  wait down, so that one gets `SIGNAL_RESTARTS`, the same way `dd` reports progress. `SIG_IGN` takes
  neither, since no handler runs and there is nothing to restart.
- `SA_RESTART` does not cover everything. It restarts `recv` and friends, but never `poll`, `select`,
  `epoll_wait`, or a socket carrying `SO_RCVTIMEO`. A program blocked in `poll_until` therefore sees
  `EINTR` for every signal regardless of the flag, and the flag only documents intent there.
- A timeout is an absolute instant, never a duration handed to each retry. `deadline_start` records
  `CLOCK_MONOTONIC` plus the timeout once, and `deadline_left_ms` recomputes what is left on every
  pass, so an `EINTR` retry shrinks the wait instead of restarting it. Passing the same `timeout_ms`
  to a retried `poll` is how a two second bound turns into an unbounded one under repeated signals.
  `CLOCK_MONOTONIC` rather than the wall clock, so a `clock_settime` jump cannot move the deadline.
- Waiting also has to stop even when the wait never gets to report a timeout. `nl_recv_until` checks
  `deadline_expired` before it polls, so a stream of `EINTR` that never lets `poll` return zero still
  ends in `ETIMEDOUT` rather than spinning.
- Testing a stop flag and then blocking is two steps, and a signal that lands between them is lost:
  the handler sets the flag with nothing left to interrupt, and the program waits forever on a quiet
  socket. `poll_until` therefore calls `ppoll`, and the long-running programs block the signals they
  handle before entering the loop and hand `ppoll` the original mask. Unblocking and blocking become
  one atomic step, so a signal delivered during the flag test stays pending and fires the moment the
  wait begins.
- `deadline_expired` answers a yes/no question, so it must not lose the sub-millisecond remainder.
  Truncating the conversion made a 5 ms deadline expire at 4 ms and a 1 ms deadline expire before it
  began. The remaining time is computed in nanoseconds and rounded up to the next millisecond, so
  zero means the instant has genuinely passed.
- **A timeout only binds a call that can return early.** Every socket here is created with
  `SOCK_NONBLOCK`, because polling before a blocking call bounds nothing: the poll answers "at least
  one byte fits", and the call then writes megabytes and sleeps inside the kernel with the deadline
  untouched. Measured on a receiver that never read: one `sendfile()` on a blocking socket held for
  20.001 s despite a 5 s deadline sitting right in front of it. With the socket non-blocking the same
  call returns in 0.000207 s, the loop sees `EAGAIN`, and the deadline fires at 5.038 s. `splice`
  additionally passes `SPLICE_F_NONBLOCK`, since the pipe end has flags of its own.
- Timeouts are idle timeouts, restarted when bytes actually move, not caps on a whole transfer. A
  100 MB file is not a failure; five seconds of no progress is. The restart is per completed
  operation, never inside one wait's `EINTR` retry loop, which is what keeps the bound real.
- No mutable globals. The one exception is that stop flag. Shared counters are C11 atomics:
  `nl_next_seq` hands out netlink sequence numbers with `atomic_fetch_add_explicit`.
- Only thread-safe library calls: `strerror_r` not `strerror`, `inet_ntop` not `inet_ntoa`,
  `if_indextoname` into a caller buffer.
- `common/log.c` formats a whole line into a local buffer and emits it with one `fprintf`. Three
  stdio calls per message would let concurrent threads interleave mid-line.

## linux/

TCP and UDP echo, four zero-copy transfer paths, and six netlink programs, built with a Makefile.
Every echo and transfer server handles one connection and exits.

### build

```sh
make -C linux            # binaries at linux/bin/<topic>/<name>
make -C linux GPROF=1    # add -pg for gprof / mcount-based uftrace
make -C linux clean
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

- `common/net_util.{c,h}` — BSD helpers. Every blocking step takes a deadline and an optional signal
  mask: `tcp_listen`, `tcp_connect`, `tcp_accept`, `udp_bind`, `udp_connect`, `wait_ready`,
  `recv_until`, `recvfrom_until`, `send_all_until`, `set_nonblocking`. `format_addr` and
  `format_peer` render a peer as `address:port`. `ignore_sigpipe`, `install_signal_handler`,
  `install_stop_handlers`, `block_signals` and `stop_requested` cover the signal side; a stop request
  surfaces as `ECANCELED` out of any wait, so shutdown travels the same path as an error.
- `common/deadline.{c,h}` — absolute monotonic deadlines: `deadline_start`, `deadline_left_ms`,
  `deadline_expired`, `poll_until`.
- `common/log.{c,h}` — `log_msg` and `log_errno` write `program: function(): text` to stderr, so no
  call site repeats its own name. Results go to stdout with `printf`; diagnostics go to stderr.
- `netlink/nl_util.{c,h}` — netlink helpers: `nl_open`, `nl_join_group`, `nl_send`, `nl_recv`,
  `nl_recv_until`, `nl_add_attr`, `nl_parse_attrs`, `nl_check_error`, `nl_next_seq`. It sits beside
  the programs that use it rather than in `common/`, because `netlink/` is its only consumer.
  `common/` is for code more than one topic uses; a helper is promoted there when a second caller
  appears, which is why iproute2 keeps `lib/libnetlink.c` in a library directory (`ip`, `ss`, `tc`
  and `bridge` all use it) while subsystem-local helpers stay in their subsystem. The Makefile's
  `TOPIC_LIB_SRCS` marks such a file as a library object so it is compiled but not linked as a
  program of its own.
- `tcp/` — `echo_server.c`, `echo_client.c`.
- `udp/` — `echo_server.c`, `echo_client.c`.
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
  `send_all` retries a stream. A partial write is reported as `EMSGSIZE` instead.
- `recvfrom` uses `MSG_TRUNC`, which returns the real message length even when it did not fit, so
  an oversized message is caught rather than silently parsed from a truncated buffer.
- The sender is checked: any local process can address a netlink socket if it knows the port id, so
  a message whose `nl_pid` is not 0 is rejected. Binding leaves `nl_pid` at 0 as well, letting the
  kernel assign a unique port id; hardcoding `getpid()` breaks the moment a second netlink socket
  exists in the process.
- `NLMSG_OK`, `NLMSG_NEXT`, `RTA_OK` and `RTA_NEXT` decrement the length they are given, and the
  last attribute can be shorter than its own alignment. The length variable has to be `int`; a
  `size_t` wraps to a huge value at the tail and the walk runs off the buffer.
- Attribute types carry `NLA_F_NESTED` in their high bits, so `nl_parse_attrs` masks with
  `NLA_TYPE_MASK` before indexing. Without the mask every nested attribute is skipped.
- A dump that raced with a change comes back with `NLM_F_DUMP_INTR` set and is inconsistent. The
  dump programs report it instead of printing a half-truth.
- Nothing waits without a bound. `nl_recv_until` takes a `struct deadline`, so a dump or an
  `NLM_F_ACK` reply that never arrives ends in `ETIMEDOUT` instead of hanging. `route_monitor` and
  `nflog_listen` pass `DEADLINE_FOREVER` because waiting is their job; every request/reply exchange
  passes a real bound.
- A dump request for links has to carry `IFLA_EXT_MASK`. Without it the kernel never sizes the dump
  buffer per device, and an interface whose `RTM_NEWLINK` message does not fit is left out of the
  answer with no error anywhere. A netns holding one device with 400 long altnames reproduces it:
  the dump silently returns one interface fewer than `ip link` lists.
- `NLMSG_DONE` carries the dump's exit status in its payload. A dump the kernel abandoned part-way
  still ends with `NLMSG_DONE`, so treating the message itself as success reports a truncated result
  as a complete one. `nl_check_done` reads that `int` and turns it back into `errno`.
- `ENOBUFS` on a multicast socket means the kernel dropped events this reader was too slow to take.
  It is not a broken socket: the right response is to record the gap and keep reading, which is why
  `route_monitor` counts overruns instead of exiting, and asks for a larger `SO_RCVBUF` up front.
- The read buffer bounds what a single message may be. `nflog_listen` therefore asks the kernel for
  a copy range that fits in it; requesting 64 KiB of every packet into a smaller buffer turns the
  first large packet into `EMSGSIZE`. `nl_recv` reports that rather than parsing a truncated
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
|                                    SendAll, SetNonBlocking, LOG_MSG, NET_PERROR
|-- Tcp/TcpEchoServer/
`-- Tcp/TcpEchoClient/
```

The two trees answer the same questions with each platform's own vocabulary rather than a shared
abstraction: `tcp_listen` against `ListenTcp`, `log_errno` against `NET_PERROR`, `errno` against
`WSAGetLastError`. Style is enforced per tree by the root `.editorconfig` (`[Winsock/**]` section)
and `Winsock/.clang-format` (Allman braces, forced braces on one-line bodies, left-aligned
pointers). `SortIncludes` stays off there because `<winsock2.h>` must precede `<windows.h>`.
