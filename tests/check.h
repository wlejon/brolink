#pragma once
// Minimal test harness. Checks never abort and are never compiled out: every
// failure is printed with its location and counted, and main() returns
// non-zero when any failed. A watchdog turns a hang into a reported failure.

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <crtdbg.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace check {

inline std::atomic<int> g_failures{0};
inline std::atomic<int> g_checks{0};
inline std::atomic<const char*> g_phase{"init"};
inline std::atomic<long long> g_deadline_ms{0};

inline void fail(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::printf("FAIL %s:%d: %s\n", file, line, what.c_str());
    std::fflush(stdout);
}

inline int finish(const char* name) {
    std::printf("[%s] %d checks, %d failed\n", name, g_checks.load(), g_failures.load());
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}

inline long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Never stop on a modal error dialog: a test must fail, not wait for a click.
inline void no_error_dialogs() {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
}

// Name the phase and give it `seconds` before the watchdog fails the test.
inline void phase(const char* name, int seconds = 60) {
    std::printf("-- %s\n", name);
    std::fflush(stdout);
    g_phase = name;
    g_deadline_ms = now_ms() + seconds * 1000LL;
}

inline void start_watchdog(const char* test_name) {
    no_error_dialogs();
    std::thread([test_name] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            long long d = g_deadline_ms.load();
            if (d && now_ms() > d) {
                std::printf("FAIL HANG in phase '%s' (watchdog)\n", g_phase.load());
                ++g_failures;
                finish(test_name);
                std::_Exit(1);
            }
        }
    }).detach();
}

}  // namespace check

#define CHECK(cond)                                                         \
    do {                                                                    \
        ++::check::g_checks;                                                \
        if (!(cond)) ::check::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_MSG(cond, msg)                                                                       \
    do {                                                                                           \
        ++::check::g_checks;                                                                       \
        if (!(cond)) ::check::fail(__FILE__, __LINE__, std::string("CHECK(" #cond "): ") + (msg)); \
    } while (0)
