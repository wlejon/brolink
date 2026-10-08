#include "brolink/paths.h"
#include "win_util.h"

#include <sddl.h>

#include <cstdlib>

namespace brolink {

namespace {

std::string user_sid_string() {
    std::vector<unsigned char> sid = win::current_user_sid();
    if (sid.empty()) return {};
    wchar_t* s = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<PSID>(sid.data()), &s)) return {};
    std::string out = win::to_utf8(s);
    LocalFree(s);
    return out;
}

}  // namespace

std::string local_address(std::string_view app, std::string_view name, std::string* err) {
    if (!valid_name(app)) {
        if (err) *err = "invalid application name '" + std::string(app) + "'";
        return {};
    }
    if (!valid_name(name)) {
        if (err) *err = "invalid name '" + std::string(name) + "'";
        return {};
    }
    std::string sid = user_sid_string();
    if (sid.empty()) {
        if (err) *err = "cannot determine the user's SID: " + win::error_text(GetLastError());
        return {};
    }
    return "\\\\.\\pipe\\" + std::string(app) + "-" + sid + "-" + std::string(name);
}

std::string runtime_dir(std::string_view app, std::string* err) {
    if (!valid_name(app)) {
        if (err) *err = "invalid application name '" + std::string(app) + "'";
        return {};
    }
    std::wstring base;
    if (const wchar_t* l = _wgetenv(L"LOCALAPPDATA"); l && *l) base = l;
    else if (const wchar_t* t = _wgetenv(L"TEMP"); t && *t) base = t;
    if (base.empty()) {
        if (err) *err = "neither LOCALAPPDATA nor TEMP is set";
        return {};
    }
    std::wstring dir = base + L"\\" + win::to_wide(app);
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        if (err) *err = "cannot create " + win::to_utf8(dir) + ": " + win::error_text(GetLastError());
        return {};
    }
    return win::to_utf8(dir);
}

std::string current_executable() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return win::to_utf8(buf);
        }
        buf.resize(buf.size() * 2);
    }
}

uint64_t current_pid() { return GetCurrentProcessId(); }

}  // namespace brolink
