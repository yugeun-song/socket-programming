# socket-programming

Socket programming study with the Windows and Linux ecosystems kept **fully separate**.
There is no cross-platform abstraction layer: each tree is implemented natively and is
idiomatic to its platform down to naming, brace style, line endings, and build system.

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

## linux/

Native BSD sockets, no shim. Built with a hand-written Makefile.

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
```

### layout

- `common/` — shared BSD helpers: `tcp_listen`, `tcp_connect`, `udp_bind`, `udp_connect`,
  `send_all`, `set_nonblocking`.
- `tcp/` — `echo_server.c`, `echo_client.c`.
- `udp/` — `echo_server.c`, `echo_client.c`.
- `epoll/`, `unix_domain/` — reserved topics.

### profiling & tracing

The always-on flags (`-ggdb3`, frame pointers, no sibling-call optimization, async unwind
tables) make the binaries first-class targets for perf, strace, gdb, valgrind, and uftrace
dynamic tracing (`uftrace record -P. ./linux/bin/tcp/echo_client`). `-pg` for gprof /
mcount-based uftrace is opt-in via `make -C linux GPROF=1`.

## Winsock/

Native Winsock, developed on a Windows machine in Visual Studio 2026 (it cannot be built or
validated from the Linux side of this repo). This repo ships only the VS-recognized style
config — the root `.editorconfig` (`[Winsock/**]` section) and `Winsock/.clang-format`
(Allman braces, forced braces on one-line bodies, left-aligned pointers). The solution,
projects, and dev settings are created and maintained there.
