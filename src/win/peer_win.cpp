#include "brolink/peer.h"
#include "win_util.h"

#include <lmcons.h>

namespace brolink {

std::string current_user() {
    wchar_t name[UNLEN + 1];
    DWORD n = UNLEN + 1;
    if (!GetUserNameW(name, &n)) return {};
    return win::to_utf8(name);
}

ProcessOrigin describe_process(uint64_t pid) {
    // Another process's environment is not readable here without reading
    // its memory: Windows says who, not whether ssh started it.
    ProcessOrigin o;
    o.pid = pid;
    o.user = current_user();
    return o;
}

}  // namespace brolink
