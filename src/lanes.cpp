#include "brolink/lanes.h"

#include "brolink/random.h"

#include <algorithm>

namespace brolink::lanes {

bool valid_lane_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaxLaneName) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::optional<Token> Token::generate(std::string* err) {
    Token t;
    if (!random_bytes(t.bytes.data(), t.bytes.size(), err)) return std::nullopt;
    return t;
}

bool Token::equals(const Token& other) const noexcept {
    uint8_t diff = 0;
    for (size_t i = 0; i < kTokenBytes; ++i) diff |= uint8_t(bytes[i] ^ other.bytes[i]);
    return diff == 0;
}

std::string Token::hex() const {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string s;
    s.reserve(kTokenBytes * 2);
    for (uint8_t b : bytes) {
        s.push_back(kDigits[b >> 4]);
        s.push_back(kDigits[b & 15]);
    }
    return s;
}

std::optional<Token> Token::from_hex(std::string_view s) {
    if (s.size() != kTokenBytes * 2) return std::nullopt;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    Token t;
    for (size_t i = 0; i < kTokenBytes; ++i) {
        const int hi = nibble(s[2 * i]), lo = nibble(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        t.bytes[i] = uint8_t(hi << 4 | lo);
    }
    return t;
}

void Grant::write(wire::Writer& w) const {
    w.varint(session);
    w.raw(std::string_view(reinterpret_cast<const char*>(token.bytes.data()), kTokenBytes));
}

bool Grant::read(wire::Reader& r) {
    session = r.varint();
    std::string_view t = r.raw(kTokenBytes);
    if (!r.ok()) return false;
    std::copy(t.begin(), t.end(), token.bytes.begin());
    return true;
}

void Join::write(wire::Writer& w) const {
    w.varint(session);
    w.raw(std::string_view(reinterpret_cast<const char*>(token.bytes.data()), kTokenBytes));
    w.str(lane);
}

bool Join::read(wire::Reader& r) {
    session = r.varint();
    std::string_view t = r.raw(kTokenBytes);
    lane = r.str_max(kMaxLaneName);
    if (!r.ok()) return false;
    std::copy(t.begin(), t.end(), token.bytes.begin());
    return true;
}

const char* join_result_name(JoinResult r) noexcept {
    switch (r) {
        case JoinResult::Joined: return "joined";
        case JoinResult::NoSession: return "no such session";
        case JoinResult::BadToken: return "wrong token";
        case JoinResult::BadLane: return "invalid lane name";
        case JoinResult::LaneTaken: return "lane already joined";
        case JoinResult::Already: return "connection already in a session";
    }
    return "?";
}

std::optional<Grant> Registry::open(ConnId control, std::string* err) {
    if (members_.count(control)) {
        if (err) *err = "the connection is already in a session";
        return std::nullopt;
    }
    std::optional<Token> token = Token::generate(err);
    if (!token) return std::nullopt;
    const uint64_t id = next_session_++;
    Session& s = sessions_[id];
    s.token = *token;
    s.control = control;
    members_[control] = Member{id, {}};
    return Grant{id, *token};
}

JoinResult Registry::join(ConnId conn, const Join& j) {
    if (members_.count(conn)) return JoinResult::Already;
    auto it = sessions_.find(j.session);
    if (it == sessions_.end()) return JoinResult::NoSession;
    Session& s = it->second;
    if (!s.token.equals(j.token)) return JoinResult::BadToken;
    if (!valid_lane_name(j.lane)) return JoinResult::BadLane;
    if (s.used.count(j.lane)) return JoinResult::LaneTaken;
    s.used.insert(j.lane);
    s.lanes[j.lane] = conn;
    members_[conn] = Member{j.session, j.lane};
    return JoinResult::Joined;
}

const Registry::Member* Registry::member(ConnId conn) const {
    auto it = members_.find(conn);
    return it == members_.end() ? nullptr : &it->second;
}

std::optional<ConnId> Registry::lane(uint64_t session, std::string_view name) const {
    auto it = sessions_.find(session);
    if (it == sessions_.end()) return std::nullopt;
    auto l = it->second.lanes.find(name);
    if (l == it->second.lanes.end()) return std::nullopt;
    return l->second;
}

std::optional<ConnId> Registry::control(uint64_t session) const {
    auto it = sessions_.find(session);
    if (it == sessions_.end()) return std::nullopt;
    return it->second.control;
}

std::vector<std::pair<std::string, ConnId>> Registry::lanes(uint64_t session) const {
    std::vector<std::pair<std::string, ConnId>> out;
    auto it = sessions_.find(session);
    if (it == sessions_.end()) return out;
    for (const auto& [name, id] : it->second.lanes) out.emplace_back(name, id);
    return out;
}

std::vector<ConnId> Registry::closed(ConnId conn) {
    std::vector<ConnId> close;
    auto m = members_.find(conn);
    if (m == members_.end()) return close;
    const Member member = m->second;
    members_.erase(m);
    auto s = sessions_.find(member.session);
    if (s == sessions_.end()) return close;
    if (!member.control()) {
        s->second.lanes.erase(member.lane);
        return close;
    }
    for (const auto& [name, id] : s->second.lanes) {
        members_.erase(id);
        close.push_back(id);
    }
    sessions_.erase(s);
    return close;
}

}  // namespace brolink::lanes
