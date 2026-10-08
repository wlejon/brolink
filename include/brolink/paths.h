#pragma once
// Where a local server listens, per user, and other runtime paths.
//
// Each application names its own endpoints: local_address("bromux", "default").
//
// POSIX: a Unix socket <dir>/<name>.sock, where <dir> is $XDG_RUNTIME_DIR/<app>
// when that is set (and ours), else ${TMPDIR:-/tmp}/<app>-<uid>. The
// directory is created 0700 and must be owned by the user and not
// group/world accessible, or it is refused (someone else could otherwise
// plant a socket there). The socket itself is 0600, and the server checks
// every client's uid (SO_PEERCRED / getpeereid) as the client checks the
// server's.
//
// Windows: a named pipe \\.\pipe\<app>-<user SID>-<name>, created with
// FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_REJECT_REMOTE_CLIENTS and a protected
// DACL that grants only the user's SID; the client checks that the process
// serving the pipe runs as the same user before speaking (no squatting).

#include <cstdint>
#include <string>
#include <string_view>

namespace brolink {

// Application and endpoint names are 1..64 of [A-Za-z0-9_.-], not "." or "..".
[[nodiscard]] bool valid_name(std::string_view name) noexcept;

// The address endpoint `name` of application `app` listens on (a socket
// path or a pipe name), creating the private directory a socket lives in.
// Empty on failure (`err` says why).
[[nodiscard]] std::string local_address(std::string_view app, std::string_view name, std::string* err = nullptr);

// A per-user directory for the application's files (logs): the socket
// directory on POSIX, %LOCALAPPDATA%\<app> on Windows. Created if missing.
[[nodiscard]] std::string runtime_dir(std::string_view app, std::string* err = nullptr);

// Absolute path of the running executable.
[[nodiscard]] std::string current_executable();
[[nodiscard]] uint64_t current_pid();
[[nodiscard]] uint64_t unix_time_ms();

}  // namespace brolink
