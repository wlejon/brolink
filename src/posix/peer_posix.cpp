#include "brolink/peer.h"

#include <pwd.h>
#include <unistd.h>

#include <fstream>
#include <iterator>
#include <string>

namespace brolink {

std::string current_user() {
    struct passwd pw {};
    struct passwd* found = nullptr;
    char buf[4096];
    if (getpwuid_r(::getuid(), &pw, buf, sizeof buf, &found) == 0 && found && found->pw_name) return found->pw_name;
    return std::to_string(::getuid());
}

ProcessOrigin describe_process(uint64_t pid) {
    ProcessOrigin o;
    o.pid = pid;
    o.user = current_user();
    if (!pid) return o;
#if defined(__linux__)
    // NUL-separated NAME=value; readable for the user's own processes.
    std::ifstream in("/proc/" + std::to_string(pid) + "/environ", std::ios::binary);
    if (!in) return o;
    const std::string env((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string client;
    for (size_t at = 0; at < env.size();) {
        size_t end = env.find('\0', at);
        if (end == std::string::npos) end = env.size();
        const std::string_view kv(env.data() + at, end - at);
        if (kv.rfind("SSH_CONNECTION=", 0) == 0) o.ssh_connection = std::string(kv.substr(15));
        else if (kv.rfind("SSH_CLIENT=", 0) == 0) client = std::string(kv.substr(11));
        at = end + 1;
    }
    // SSH_CONNECTION is "client port server port"; SSH_CLIENT "client port port".
    const std::string& from = o.ssh_connection.empty() ? client : o.ssh_connection;
    o.ssh_client = from.substr(0, from.find(' '));
#endif
    return o;
}

}  // namespace brolink
