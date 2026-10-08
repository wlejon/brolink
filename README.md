# brolink

The local IPC and ssh transport under the bro ecosystem's services (bromux,
broremote): message framing, a per-user local listener with peer-credential
checks, blocking byte streams, the `ssh host <tool> proxy` relay, and lanes
(one session as a bundle of independent connections). C++20, platform APIs
only, no dependencies. MIT.

## What is in it

| Header | What |
|--------|------|
| `brolink/wire.h` | `Writer` / `Reader` (LE fixed-width, LEB128 `varint`, zigzag `svarint`, `f32`, `str`, `bytes`, every read bounds-checked), `frame_message` / `make_message`, `MessageSplitter` (u32 length + u16 type framing, a per-protocol maximum) |
| `brolink/paths.h` | `local_address(app, name)`: where a server listens; `runtime_dir(app)`, `valid_name`, `current_executable`, `current_pid`, `unix_time_ms` |
| `brolink/loop.h` | `EventLoop` + `LoopHandler`: the server side. A listener and nonblocking connections, callbacks on the thread calling `run_once`, queued writes, `pending_output` for the caller's own flow control, `wake()` from any thread |
| `brolink/stream.h` | `Stream` (blocking read / write / shutdown from any thread); `connect_local`, `spawn_stream` (a child's stdio, how ssh runs), `stdio_stream`, `spawn_detached`, `Process` |
| `brolink/proxy.h` | `run_proxy` / `relay`: stdio to a local server; pty mode (`ssh -tt`: a raw, binary-clean terminal, then a ready marker) and `await_ready` for the client side |
| `brolink/lanes.h` | `Token` (32 bytes from the OS CSPRNG), `Grant` / `Join` bodies, `Registry`: sessions, their control lane and named lanes |
| `brolink/random.h` | `random_bytes`: getrandom / arc4random_buf / BCryptGenRandom |

## Transport

- **Linux / macOS:** a Unix socket `$XDG_RUNTIME_DIR/<app>/<name>.sock`
  (else `${TMPDIR:-/tmp}/<app>-<uid>/`). The directory is created 0700 and
  refused if it is not a private directory of ours; the socket is 0600; a
  `<socket>.lock` held with `flock` decides which server owns the address,
  so a crashed server's socket is reclaimed. The server checks each client's
  uid (`SO_PEERCRED` / `getpeereid`), and the client checks the server's.
- **Windows:** a named pipe `\\.\pipe\<app>-<user SID>-<name>`, created with
  `FILE_FLAG_FIRST_PIPE_INSTANCE`, `PIPE_REJECT_REMOTE_CLIENTS` and a
  protected DACL granting only the user's SID, served by overlapped I/O on an
  I/O completion port. The client refuses a pipe whose serving process runs as
  anyone else (no squatting).
- **Remote:** `ssh host <tool> proxy`. The proxy is a byte relay, so the
  protocol runs end to end. With `ssh -tt` (which makes OpenSSH set
  `TCP_NODELAY`) the proxy's pty mode makes the terminal raw and writes a
  ready marker before any protocol byte; `await_ready` reads past it.

## Lanes

A session is a bundle of independent byte streams. Its first connection, the
control lane, is issued a `Grant`: a session id and a token from the OS
CSPRNG, made for that session alone. Any further connection presents the
token in a `Join` as its first message and becomes a named lane of that
session, so what flows on one lane never queues behind another (a viewer's
input never waits behind video). Each lane may arrive by any transport: a
second local connection, or a second `ssh ... proxy`. Each lane name joins at
most once per session; the session and all its lanes end when the control
lane does. `Grant` and `Join` are message bodies the application carries in
its own message types, so brolink fixes neither the protocol nor the
transport.

## Building

```
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

Consumers add it as a sibling: `add_subdirectory(../brolink)` and link
`brolink`.
