#pragma once
// Who is at the other end of a local connection.
//
// A local endpoint only ever admits the user's own processes (paths.h), so
// the question is not which user but which process, and how it got here: a
// program in the session, or one an ssh login started (`ssh box broremote
// proxy`, `ssh box bro-ctl ...`). EventLoop::peer_pid() names the process;
// describe_process() says what can be told about it.
//
// None of this is a credential: everything here was true of the process
// when it connected, and is offered so a server can say who asked
// (an audit log, a request's sender), not decide who may.

#include <cstdint>
#include <string>

namespace brolink {

struct ProcessOrigin {
    uint64_t pid = 0;        // 0: not known
    std::string user;        // the login name it runs as (ours: the endpoint admits no one else)
    // Its ssh session, when an ssh login started it (SSH_CONNECTION in its
    // environment: Linux reads /proc/<pid>/environ); empty for a process of
    // the local session, and wherever the environment cannot be read
    // (macOS, Windows).
    std::string ssh_client;      // the client's address ("10.1.0.5")
    std::string ssh_connection;  // the whole SSH_CONNECTION: client address and port, server address and port
};

// What can be told about process `pid` (this user's): its user, and its ssh
// session if it has one. pid 0 describes nothing but the user.
[[nodiscard]] ProcessOrigin describe_process(uint64_t pid);

// The login name of the user running this process.
[[nodiscard]] std::string current_user();

}  // namespace brolink
