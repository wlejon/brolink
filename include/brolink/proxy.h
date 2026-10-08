#pragma once
// The proxy: the remote end of `ssh host <tool> proxy`. A byte relay between
// this process's stdin/stdout and a local server, so a protocol runs end to
// end between a remote client and the server with no knowledge of it here
// (and no state to lose on a reconnect).
//
// The pty mode: run under a terminal ssh allocated (`ssh -tt`, which is what
// makes OpenSSH turn Nagle off and mark the session low-delay), the proxy
// puts that terminal in raw mode (no echo, no line editing, no character
// translation: a binary-clean pipe) and only then writes a ready marker,
// before any protocol byte. A client must not send until it has read the
// marker (await_ready), or the terminal's cooked mode would echo and edit
// what it sent. Errors go to stdout before the marker too, since a terminal
// merges stderr into it.

#include "brolink/stream.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace brolink {

// The marker a pty-mode proxy writes once its terminal is raw.
inline constexpr std::string_view kProxyReady = "\nbrolink-proxy-ready\n";

struct ProxyOptions {
    bool pty = false;
    // What precedes an error written to stdout in pty mode ("bromux proxy").
    std::string name = "proxy";
    // The marker written in pty mode.
    std::string ready_marker = std::string(kProxyReady);
};

// Opens the server connection; null with *err on failure.
using Connector = std::function<std::unique_ptr<Stream>(std::string* err)>;

// Relay stdin/stdout to the stream `connect` opens until either side
// closes. Returns the process exit code (0, or 1 when the connection could
// not be made, with *err set).
int run_proxy(const Connector& connect, const ProxyOptions& options, std::string* err);

// Relay two streams to each other until either side closes; returns when
// `b` -> `a` ends (and shuts both down). The `a` -> `b` direction runs on a
// detached thread, so a read blocked on `a` cannot hold up the return.
void relay(std::shared_ptr<Stream> a, std::shared_ptr<Stream> b);

// Put this process's stdin/stdout terminals (if they are terminals) in raw
// mode: a binary-clean pipe. A no-op on Windows.
void make_stdio_raw();

// Wraps a stream to a pty-mode proxy: the first read or write first reads up
// to and including `marker`, discarding it. If the stream ends first, the
// first write fails and diagnostics() includes what came instead (the
// proxy's error, or the remote shell's).
std::unique_ptr<Stream> await_ready(std::unique_ptr<Stream> inner, std::string marker = std::string(kProxyReady));

}  // namespace brolink
