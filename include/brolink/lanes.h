#pragma once
// Lanes: one session as a bundle of independent byte streams.
//
// A session starts with one connection, its control lane. The server opens
// a session for it and sends it a Grant: a session id and a token of
// kTokenBytes from the OS CSPRNG (random.h), made for that session alone and
// never reused. A further connection joins the session as a named lane by
// presenting the token (a Join) as its first message; each lane is then its
// own connection, so what flows on one never queues behind another (a
// viewer's input never waits behind video). Each lane name joins at most
// once per session, so a token seen after its lanes are open opens nothing
// new; the session, and with it every lane, ends when its control lane does.
//
// Nothing here knows the transport: a lane connection may be a direct local
// connection or ssh + proxy, the same as the control lane or not. Nor does
// it fix the message framing: Grant and Join are bodies the application
// carries in its own message types (wire.h), and the Registry is the
// server's bookkeeping, keyed by whatever connection ids it uses (loop.h's
// ConnId). The Registry is not thread-safe: keep it on the I/O thread.

#include "brolink/loop.h"
#include "brolink/wire.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace brolink::lanes {

inline constexpr size_t kTokenBytes = 32;
inline constexpr size_t kMaxLaneName = 64;

// Lane names are 1..64 of [A-Za-z0-9_.-].
[[nodiscard]] bool valid_lane_name(std::string_view name) noexcept;

struct Token {
    std::array<uint8_t, kTokenBytes> bytes{};
    // A fresh token from the OS CSPRNG; nullopt (with *err) when it refused.
    static std::optional<Token> generate(std::string* err = nullptr);
    // Constant-time comparison.
    [[nodiscard]] bool equals(const Token& other) const noexcept;
    [[nodiscard]] std::string hex() const;
    static std::optional<Token> from_hex(std::string_view s);
};

// Server -> the control connection: what joins a lane to this session.
// Body: varint session, kTokenBytes raw token.
struct Grant {
    uint64_t session = 0;
    Token token;
    void write(wire::Writer& w) const;
    bool read(wire::Reader& r);
};

// A lane connection's first message: which session, the proof, which lane.
// Body: varint session, kTokenBytes raw token, str lane.
struct Join {
    uint64_t session = 0;
    Token token;
    std::string lane;
    void write(wire::Writer& w) const;
    bool read(wire::Reader& r);
};

enum class JoinResult : uint8_t {
    Joined = 0,
    NoSession = 1,  // no live session has that id
    BadToken = 2,   // the token is not the session's
    BadLane = 3,    // not a valid lane name
    LaneTaken = 4,  // that lane already joined this session (once per session)
    Already = 5,    // this connection is already in a session
};
[[nodiscard]] const char* join_result_name(JoinResult) noexcept;

class Registry {
public:
    struct Member {
        uint64_t session = 0;
        std::string lane;  // empty: the control lane
        [[nodiscard]] bool control() const noexcept { return lane.empty(); }
    };

    // Opens a session whose control lane is `control`; the Grant is what to
    // send it. nullopt (with *err) when the CSPRNG failed or `control` is
    // already in a session.
    std::optional<Grant> open(ConnId control, std::string* err = nullptr);
    // `conn` presents `join`. On Joined it is the session's lane `join.lane`.
    JoinResult join(ConnId conn, const Join& join);

    // The session and lane of a connection; null when it is in none.
    [[nodiscard]] const Member* member(ConnId conn) const;
    // The connection holding a session's lane, or its control lane.
    [[nodiscard]] std::optional<ConnId> lane(uint64_t session, std::string_view name) const;
    [[nodiscard]] std::optional<ConnId> control(uint64_t session) const;
    // Every lane (not the control) of a session.
    [[nodiscard]] std::vector<std::pair<std::string, ConnId>> lanes(uint64_t session) const;

    // `conn` has closed. Returns the connections the caller must now close:
    // when `conn` was a control lane its session ends with every lane;
    // a lane closing ends only itself (its name stays used).
    std::vector<ConnId> closed(ConnId conn);

    [[nodiscard]] size_t sessions() const noexcept { return sessions_.size(); }

private:
    struct Session {
        Token token;
        ConnId control = 0;
        std::map<std::string, ConnId, std::less<>> lanes;  // live lanes
        std::set<std::string, std::less<>> used;          // every lane name ever joined
    };
    std::unordered_map<uint64_t, Session> sessions_;
    std::unordered_map<ConnId, Member> members_;
    uint64_t next_session_ = 1;
};

}  // namespace brolink::lanes
