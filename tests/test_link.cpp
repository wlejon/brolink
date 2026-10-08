// The transport end to end: the event loop serving local connections, the
// proxy relaying a spawned child's stdio to it, and a lane session whose
// lanes arrive by both routes.
//
// The executable is also its own helper: `test_link echo` relays stdin to
// stdout, `test_link proxy <address> [--pty]` is a proxy, `test_link exit N`
// exits with N.
#include "brolink/lanes.h"
#include "brolink/loop.h"
#include "brolink/paths.h"
#include "brolink/proxy.h"
#include "brolink/stream.h"
#include "check.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

using namespace brolink;

namespace {

std::string g_self;

// A tiny protocol over the framing: the server echoes Data, opens sessions
// for Hello, joins lanes, and reports on the control lane which lane Data
// arrived on.
enum Msg : uint16_t {
    Hello = 1,    // -> Grant
    Grant = 2,    // lanes::Grant
    Join = 3,     // lanes::Join -> Joined
    Joined = 4,   // u8 JoinResult
    Data = 5,     // echoed on the same connection; also Seen on the control lane when on a lane
    Seen = 6,     // str lane, str data
    Bulk = 7,     // varint n -> a Data of n bytes
};

class Server final : public LoopHandler {
public:
    explicit Server(std::string address) : address_(std::move(address)) { loop_ = EventLoop::create(*this); }
    ~Server() override { stop(); }

    bool start(std::string& err, bool& in_use) {
        if (!loop_->listen(address_, err, in_use)) return false;
        thread_ = std::thread([this] {
            while (!stop_) loop_->run_once(50);
            loop_->close_listener();
        });
        return true;
    }
    void stop() {
        stop_ = true;
        if (thread_.joinable()) {
            loop_->wake();
            thread_.join();
        }
    }
    size_t connections() {
        std::lock_guard<std::mutex> lk(m_);
        return open_;
    }
    size_t max_pending() const { return max_pending_; }

    void on_accept(ConnId id) override {
        std::lock_guard<std::mutex> lk(m_);
        ++open_;
        conns_[id];
    }
    void on_data(ConnId id, const char* data, size_t n) override {
        wire::MessageSplitter& sp = conns_[id];
        sp.feed(data, n);
        wire::MessageSplitter::Message m;
        while (sp.next(m)) handle(id, m.type, m.payload);
        if (sp.error()) loop_->close(id, false);
    }
    void on_closed(ConnId id) override {
        conns_.erase(id);
        for (ConnId lane : reg_.closed(id)) loop_->close(lane, false);
        std::lock_guard<std::mutex> lk(m_);
        --open_;
    }

private:
    void send(ConnId id, uint16_t type, const std::string& body) {
        loop_->write(id, wire::make_message(type, body));
        max_pending_ = std::max(max_pending_.load(), loop_->pending_output(id));
    }

    void handle(ConnId id, uint16_t type, std::string_view payload) {
        wire::Reader r(payload);
        switch (type) {
            case Hello: {
                auto g = reg_.open(id);
                if (!g) return loop_->close(id, false);
                wire::Writer w;
                g->write(w);
                return send(id, Grant, w.take());
            }
            case Join: {
                lanes::Join j;
                lanes::JoinResult res = j.read(r) ? reg_.join(id, j) : lanes::JoinResult::BadLane;
                send(id, Joined, std::string(1, char(res)));
                if (res != lanes::JoinResult::Joined) loop_->close(id, true);
                return;
            }
            case Data: {
                send(id, Data, std::string(payload));
                const lanes::Registry::Member* mem = reg_.member(id);
                if (mem && !mem->control()) {
                    wire::Writer w;
                    w.str(mem->lane);
                    w.str(payload);
                    send(*reg_.control(mem->session), Seen, w.take());
                }
                return;
            }
            case Bulk: {
                const uint64_t n = r.varint();
                std::string body(size_t(n), '\0');
                for (size_t i = 0; i < body.size(); ++i) body[i] = char(i * 131 + 7);
                return send(id, Data, body);
            }
        }
    }

    std::string address_;
    std::unique_ptr<EventLoop> loop_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::map<ConnId, wire::MessageSplitter> conns_;
    lanes::Registry reg_;
    std::mutex m_;
    size_t open_ = 0;
    std::atomic<size_t> max_pending_{0};
};

// Blocking message reads from a client stream.
struct Peer {
    std::unique_ptr<Stream> s;
    wire::MessageSplitter sp;
    std::string storage;

    bool send(uint16_t type, std::string_view body) { return s->write(wire::make_message(type, body)); }
    // The next message, or false when the stream ended.
    bool next(uint16_t& type, std::string& body) {
        wire::MessageSplitter::Message m;
        char buf[65536];
        while (!sp.next(m)) {
            if (sp.error()) return false;
            const size_t n = s->read(buf, sizeof buf);
            if (n == 0) return false;
            sp.feed(buf, n);
        }
        type = m.type;
        body.assign(m.payload);
        return true;
    }
};

std::string unique_name(const char* what) {
    return std::string("t-") + what + "-" + std::to_string(current_pid()) + "-" + std::to_string(unix_time_ms() % 100000);
}

void paths() {
    check::phase("paths");
    std::string err;
    CHECK(valid_name("a.b_c-9") && !valid_name("") && !valid_name("..") && !valid_name("a/b"));
    CHECK(local_address("brolink-test", "../x", &err).empty());
    CHECK(local_address("bad/app", "x", &err).empty());
    const std::string a = local_address("brolink-test", "x", &err);
    CHECK_MSG(!a.empty(), err);
#if defined(_WIN32)
    CHECK(a.rfind("\\\\.\\pipe\\brolink-test-S-", 0) == 0);
#else
    CHECK(a.size() > 7 && a.substr(a.size() - 7) == "/x.sock");
#endif
    CHECK(!runtime_dir("brolink-test", &err).empty());
    CHECK(!current_executable().empty());
}

void serve(const std::string& address) {
    check::phase("serve and echo");
    bool not_running = false;
    std::string err;
    CHECK(connect_local(address, &err, &not_running) == nullptr);
    CHECK(not_running);

    Server server(address);
    bool in_use = false;
    CHECK_MSG(server.start(err, in_use), err);
    {
        // A second server on the same address is refused, as in use.
        Server second(address);
        std::string e2;
        bool used = false;
        CHECK(!second.start(e2, used));
        CHECK(used);
    }

    std::vector<Peer> peers(3);
    for (Peer& p : peers) {
        p.s = connect_local(address, &err);
        CHECK_MSG(p.s != nullptr, err);
    }
    if (!peers[2].s) return;
    for (size_t i = 0; i < peers.size(); ++i) CHECK(peers[i].send(Data, "hello " + std::to_string(i)));
    for (size_t i = 0; i < peers.size(); ++i) {
        uint16_t t = 0;
        std::string body;
        CHECK(peers[i].next(t, body));
        CHECK(t == Data && body == "hello " + std::to_string(i));
    }

    check::phase("bulk");
    {
        Peer& p = peers[0];
        wire::Writer w;
        w.varint(24u << 20);
        CHECK(p.send(Bulk, w.data()));
        uint16_t t = 0;
        std::string body;
        CHECK(p.next(t, body));
        CHECK(t == Data && body.size() == (24u << 20));
        bool same = body.size() == (24u << 20);
        for (size_t i = 0; same && i < body.size(); i += 4093) same = body[i] == char(i * 131 + 7);
        CHECK(same);
        CHECK(server.max_pending() > 0);  // the loop queued what the pipe could not take at once
    }

    check::phase("peer close");
    peers.clear();
    for (int i = 0; i < 100 && server.connections() != 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(server.connections() == 0);
}

void lanes_test(const std::string& address) {
    check::phase("lanes");
    Server server(address);
    std::string err;
    bool in_use = false;
    CHECK_MSG(server.start(err, in_use), err);

    Peer control;
    control.s = connect_local(address, &err);
    CHECK_MSG(control.s != nullptr, err);
    if (!control.s) return;
    CHECK(control.send(Hello, ""));
    uint16_t t = 0;
    std::string body;
    CHECK(control.next(t, body) && t == Grant);
    lanes::Grant grant;
    wire::Reader gr(body);
    CHECK(grant.read(gr));

    auto join = [&](Peer& p, const std::string& lane, const lanes::Token& token) {
        lanes::Join j{grant.session, token, lane};
        wire::Writer w;
        j.write(w);
        uint16_t jt = 0;
        std::string jb;
        if (!p.send(Join, w.data()) || !p.next(jt, jb) || jt != Joined || jb.size() != 1) return lanes::JoinResult(255);
        return lanes::JoinResult(uint8_t(jb[0]));
    };

    // A lane joined directly.
    Peer input;
    input.s = connect_local(address, &err);
    CHECK(input.s != nullptr);
    CHECK(join(input, "input", grant.token) == lanes::JoinResult::Joined);

    // A lane through a proxy child (the ssh route without ssh), pty mode:
    // the client waits for the marker before its first byte.
    check::phase("lane through the proxy");
    Peer video;
    video.s = await_ready(spawn_stream({g_self, "proxy", address, "--pty"}, &err));
    CHECK_MSG(video.s != nullptr, err);
    CHECK(join(video, "video", grant.token) == lanes::JoinResult::Joined);

    // A wrong token is refused, and the connection closed.
    Peer bad;
    bad.s = connect_local(address, &err);
    lanes::Token wrong = grant.token;
    wrong.bytes[0] ^= 1;
    CHECK(join(bad, "other", wrong) == lanes::JoinResult::BadToken);
    CHECK(!bad.next(t, body));
    // A lane name joins once.
    Peer again;
    again.s = connect_local(address, &err);
    CHECK(join(again, "input", grant.token) == lanes::JoinResult::LaneTaken);

    // Data on each lane is echoed there, and the control lane hears which lane.
    check::phase("lane traffic");
    CHECK(input.send(Data, "key"));
    CHECK(video.send(Data, std::string(100000, 'v')));
    std::map<std::string, size_t> seen;
    for (int i = 0; i < 2; ++i) {
        CHECK(control.next(t, body) && t == Seen);
        wire::Reader sr(body);
        std::string lane = sr.str();
        seen[lane] = sr.str().size();
    }
    CHECK(seen["input"] == 3 && seen["video"] == 100000);
    CHECK(input.next(t, body) && t == Data && body == "key");
    CHECK(video.next(t, body) && t == Data && body.size() == 100000);

    // The control lane closing ends the session: both lanes see the end.
    check::phase("session end");
    control.s->shutdown();
    control.s.reset();
    CHECK(!input.next(t, body));
    CHECK(!video.next(t, body));
}

void children() {
    check::phase("spawned children");
    std::string err;
    auto echo = spawn_stream({g_self, "echo"}, &err);
    CHECK_MSG(echo != nullptr, err);
    if (echo) {
        std::string big(3u << 20, 'e');
        // Write from another thread: the child echoes as it reads, so a
        // single-threaded write-then-read would fill both pipes.
        std::thread w([&] { CHECK(echo->write(big)); });
        size_t got = 0;
        char buf[65536];
        while (got < big.size()) {
            const size_t n = echo->read(buf, sizeof buf);
            if (n == 0) break;
            got += n;
        }
        w.join();
        CHECK(got == big.size());
        echo.reset();  // closes its stdin: the child ends on its own
    }
    // stderr is kept for diagnostics.
    auto noisy = spawn_stream({g_self, "exit", "3"}, &err);
    CHECK(noisy != nullptr);
    if (noisy) {
        char buf[16];
        CHECK(noisy->read(buf, sizeof buf) == 0);
        for (int i = 0; i < 50 && noisy->diagnostics().empty(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(noisy->diagnostics() == "exiting with 3");
    }
    auto p = Process::spawn({g_self, "exit", "7"}, &err);
    CHECK(p != nullptr);
    int code = -1;
    CHECK(p && p->wait_for(std::chrono::seconds(30), &code));
    CHECK(code == 7);
    CHECK(spawn_stream({"brolink-no-such-program-anywhere"}, &err) == nullptr);
}

void proxy_failure() {
    check::phase("proxy failure in pty mode");
    std::string err;
    // Nothing listens: the proxy says why before (instead of) the marker.
    auto s = await_ready(spawn_stream({g_self, "proxy", local_address("brolink-test", unique_name("none")), "--pty"}, &err));
    CHECK(s != nullptr);
    if (!s) return;
    CHECK(!s->write("x"));
    CHECK(s->diagnostics().find("no server is listening") != std::string::npos);
}

int child(int argc, char** argv) {
    const std::string mode = argv[1];
    if (mode == "echo") {
        auto io = stdio_stream();
        char buf[65536];
        for (;;) {
            const size_t n = io->read(buf, sizeof buf);
            if (n == 0 || !io->write(std::string_view(buf, n))) return 0;
        }
    }
    if (mode == "exit" && argc > 2) {
        std::fprintf(stderr, "exiting with %s\n", argv[2]);
        return std::atoi(argv[2]);
    }
    if (mode == "proxy" && argc > 2) {
        const std::string address = argv[2];
        ProxyOptions opt;
        opt.pty = argc > 3 && !std::strcmp(argv[3], "--pty");
        opt.name = "test_link proxy";
        std::string err;
        return run_proxy([&](std::string* e) { return connect_local(address, e); }, opt, &err);
    }
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) return child(argc, argv);
    check::start_watchdog("test_link");
    g_self = current_executable();
    std::string err;
    paths();
    const std::string a = local_address("brolink-test", unique_name("serve"), &err);
    serve(a);
    lanes_test(local_address("brolink-test", unique_name("lanes"), &err));
    children();
    proxy_failure();
    return check::finish("test_link");
}
