#pragma once
// Blocking byte streams, and process spawning.
//
// A Stream is read by one thread and written by others (writes must be
// serialised by the caller); shutdown() from any thread unblocks a pending
// read. Kinds: a connection to a local server (paths.h names its address),
// a child process's stdin/stdout (how a client runs `ssh host <tool> proxy`),
// and this process's own stdin/stdout (the proxy's end).
//
// Local connections check who is on the other end before returning: on
// POSIX the server's uid (SO_PEERCRED / getpeereid) must be ours; on Windows
// the process serving the pipe must run as our user SID (no pipe squatting).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace brolink {

class Stream {
public:
    virtual ~Stream() = default;
    // Blocks until data arrives. Returns the bytes read; 0 at end of stream,
    // on error or after shutdown().
    virtual size_t read(char* buf, size_t n) = 0;
    // Writes everything (blocking). False when the stream is gone.
    virtual bool write(std::string_view data) = 0;
    // Unblocks read() and fails further I/O. Idempotent, any thread.
    virtual void shutdown() = 0;
    // Extra text for error messages (e.g. what an ssh child said on stderr).
    [[nodiscard]] virtual std::string diagnostics() const { return {}; }
};

// Connect to the server listening at `address` (paths.h: local_address). On
// failure returns null and sets `not_running` when nothing is listening
// there (as opposed to, say, the peer check failing).
std::unique_ptr<Stream> connect_local(const std::string& address, std::string* err = nullptr,
                                      bool* not_running = nullptr);

// Spawn argv[0] (searched on PATH) with its stdin/stdout as the stream; its
// stderr is collected for diagnostics(). shutdown() ends the child, which
// unblocks a read. Destroying the stream closes the child's stdin first, so
// a child that exits at end of input (a proxy, ssh) goes on its own; one
// still there after a moment is killed.
std::unique_ptr<Stream> spawn_stream(const std::vector<std::string>& argv, std::string* err = nullptr);

// This process's stdin (read) and stdout (write), in binary.
std::unique_ptr<Stream> stdio_stream();

// Start argv detached from this process (no console, its own session /
// process group, out of our job), not waiting for it. For auto-started servers.
bool spawn_detached(const std::vector<std::string>& argv, std::string* err = nullptr);

// A plain child process (stdio inherited), for tools and tests.
class Process {
public:
    virtual ~Process() = default;
    [[nodiscard]] virtual int64_t pid() const = 0;
    // Forceful kill (SIGKILL / TerminateProcess).
    virtual void kill() = 0;
    // Wait for exit; true with the exit code when it exited in time.
    virtual bool wait_for(std::chrono::milliseconds timeout, int* exit_code = nullptr) = 0;
    static std::unique_ptr<Process> spawn(const std::vector<std::string>& argv, std::string* err = nullptr);
};

// The Windows command line for argv, quoted the way the MSVC runtime parses
// it back (argv[0] by its own rules: quotes delimit, backslashes are
// literal). Exposed for tests; used by the spawns on Windows.
[[nodiscard]] std::string windows_command_line(const std::vector<std::string>& argv);

}  // namespace brolink
