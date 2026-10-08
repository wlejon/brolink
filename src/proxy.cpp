// The proxy relay, and the client's side of its pty handshake.
#include "brolink/proxy.h"

#include <algorithm>
#include <mutex>
#include <thread>

#if !defined(_WIN32)
#include <termios.h>
#include <unistd.h>
#endif

namespace brolink {

namespace {

constexpr size_t kUpBuffer = 64u << 10;     // client -> server: small messages
constexpr size_t kDownBuffer = 256u << 10;  // server -> client: may be bulk

class ReadyStream final : public Stream {
public:
    ReadyStream(std::unique_ptr<Stream> inner, std::string marker)
        : inner_(std::move(inner)), marker_(std::move(marker)) {}
    size_t read(char* buf, size_t n) override {
        if (!ready()) return 0;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (!leftover_.empty()) {
                const size_t k = std::min(n, leftover_.size());
                leftover_.copy(buf, k);
                leftover_.erase(0, k);
                return k;
            }
        }
        return inner_->read(buf, n);
    }
    bool write(std::string_view data) override { return ready() && inner_->write(data); }
    void shutdown() override { inner_->shutdown(); }
    std::string diagnostics() const override {
        std::string d = inner_->diagnostics();
        std::lock_guard<std::mutex> lk(m_);
        if (!found_ && !before_.empty()) {
            std::string said = before_;
            while (!said.empty() && (said.back() == '\n' || said.back() == '\r')) said.pop_back();
            d = d.empty() ? said : d + "\n" + said;
        }
        return d;
    }

private:
    // Reads up to the marker once; the reader and the first writer may both get here.
    bool ready() {
        std::lock_guard<std::mutex> rl(ready_m_);
        {
            std::lock_guard<std::mutex> lk(m_);
            if (found_ || failed_) return found_;
        }
        char buf[4096];
        for (;;) {
            const size_t n = inner_->read(buf, sizeof buf);
            std::lock_guard<std::mutex> lk(m_);
            if (n == 0) {
                failed_ = true;
                return false;
            }
            before_.append(buf, n);
            const size_t at = before_.find(marker_);
            if (at != std::string::npos) {
                leftover_ = before_.substr(at + marker_.size());
                before_.clear();
                found_ = true;
                return true;
            }
            if (before_.size() > 65536) before_.erase(0, before_.size() - 65536);
        }
    }

    std::unique_ptr<Stream> inner_;
    const std::string marker_;
    std::mutex ready_m_;
    mutable std::mutex m_;
    bool found_ = false, failed_ = false;
    std::string before_, leftover_;
};

}  // namespace

void make_stdio_raw() {
#if !defined(_WIN32)
    for (int fd : {0, 1}) {
        if (!::isatty(fd)) continue;
        termios t{};
        if (::tcgetattr(fd, &t) != 0) continue;
        ::cfmakeraw(&t);
        ::tcsetattr(fd, TCSANOW, &t);
    }
#endif
}

std::unique_ptr<Stream> await_ready(std::unique_ptr<Stream> inner, std::string marker) {
    if (!inner) return nullptr;
    return std::make_unique<ReadyStream>(std::move(inner), std::move(marker));
}

void relay(std::shared_ptr<Stream> a, std::shared_ptr<Stream> b) {
    // a -> b on its own thread (detached: a blocked read of `a`, say stdin,
    // must not hold up exit once the other side has gone).
    std::thread([a, b] {
        std::unique_ptr<char[]> buf(new char[kUpBuffer]);
        for (;;) {
            size_t n = a->read(buf.get(), kUpBuffer);
            if (n == 0 || !b->write(std::string_view(buf.get(), n))) break;
        }
        b->shutdown();  // ends the other direction too
    }).detach();

    // b -> a here.
    std::unique_ptr<char[]> buf(new char[kDownBuffer]);
    for (;;) {
        size_t n = b->read(buf.get(), kDownBuffer);
        if (n == 0 || !a->write(std::string_view(buf.get(), n))) break;
    }
    b->shutdown();
    a->shutdown();
}

int run_proxy(const Connector& connect, const ProxyOptions& options, std::string* err) {
    std::shared_ptr<Stream> io(stdio_stream());
    if (options.pty) make_stdio_raw();
    std::string e;
    std::shared_ptr<Stream> server(connect ? connect(&e) : nullptr);
    if (!server) {
        if (e.empty()) e = "cannot connect";
        // On a terminal stderr is stdout anyway; say it where the client reads.
        if (options.pty) io->write(options.name + ": " + e + "\n");
        if (err) *err = e;
        return 1;
    }
    if (options.pty && !io->write(options.ready_marker)) return 1;
    relay(io, server);
    return 0;
}

}  // namespace brolink
