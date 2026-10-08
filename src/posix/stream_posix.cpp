// POSIX streams: a Unix-socket connection to a local server and this process's stdio.
#include "brolink/stream.h"
#include "posix_util.h"

#include <sys/un.h>

#include <atomic>

namespace brolink {

namespace {

class SocketStream final : public Stream {
public:
    explicit SocketStream(int fd) : fd_(fd) {}
    ~SocketStream() override { ::close(fd_); }
    size_t read(char* buf, size_t n) override {
        for (;;) {
            if (stopped_) return 0;
            ssize_t r = ::recv(fd_, buf, n, 0);
            if (r > 0) return size_t(r);
            if (r < 0 && errno == EINTR) continue;
            return 0;
        }
    }
    bool write(std::string_view data) override {
        while (!data.empty()) {
            if (stopped_) return false;
            ssize_t r = ::send(fd_, data.data(), data.size(), posix::send_flags());
            if (r < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            data.remove_prefix(size_t(r));
        }
        return true;
    }
    void shutdown() override {
        if (!stopped_.exchange(true)) ::shutdown(fd_, SHUT_RDWR);
    }

private:
    int fd_;
    std::atomic<bool> stopped_{false};
};

// This process's stdin / stdout (not owned: never closed here).
class StdioStream final : public Stream {
public:
    size_t read(char* buf, size_t n) override {
        for (;;) {
            if (stopped_) return 0;
            ssize_t r = ::read(0, buf, n);
            if (r > 0) return size_t(r);
            if (r < 0 && errno == EINTR) continue;
            return 0;
        }
    }
    bool write(std::string_view data) override {
        while (!data.empty()) {
            if (stopped_) return false;
            ssize_t r = posix::write_pipe(1, data.data(), data.size());
            if (r < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            data.remove_prefix(size_t(r));
        }
        return true;
    }
    void shutdown() override { stopped_ = true; }

private:
    std::atomic<bool> stopped_{false};
};

}  // namespace

std::unique_ptr<Stream> connect_local(const std::string& address, std::string* err, bool* not_running) {
    if (not_running) *not_running = false;
    sockaddr_un sa{};
    sa.sun_family = AF_UNIX;
    if (address.size() >= sizeof sa.sun_path) {
        if (err) *err = "socket path too long: " + address;
        return nullptr;
    }
    std::memcpy(sa.sun_path, address.c_str(), address.size() + 1);
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        if (err) *err = "socket: " + posix::errno_text(errno);
        return nullptr;
    }
    posix::set_cloexec(fd);
    posix::no_sigpipe(fd);
    int r;
    do {
        r = ::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    } while (r != 0 && errno == EINTR);
    if (r != 0) {
        const int e = errno;
        ::close(fd);
        if (e == ENOENT || e == ECONNREFUSED) {
            if (not_running) *not_running = true;
            if (err) *err = "no server is listening at " + address;
        } else if (err) {
            *err = "cannot connect to " + address + ": " + posix::errno_text(e);
        }
        return nullptr;
    }
    if (!posix::peer_is_us(fd)) {
        ::close(fd);
        if (err) *err = "the server at " + address + " does not run as this user; refusing it";
        return nullptr;
    }
    return std::make_unique<SocketStream>(fd);
}

std::unique_ptr<Stream> stdio_stream() { return std::make_unique<StdioStream>(); }

}  // namespace brolink
