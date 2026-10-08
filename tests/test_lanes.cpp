// Lane bundling: tokens, the Grant / Join bodies and the server's registry.
#include "brolink/lanes.h"
#include "brolink/random.h"
#include "check.h"

#include <set>

using namespace brolink;
using namespace brolink::lanes;

namespace {

void tokens() {
    check::phase("tokens");
    std::string err;
    std::set<std::string> seen;
    for (int i = 0; i < 64; ++i) {
        auto t = Token::generate(&err);
        CHECK_MSG(t.has_value(), err);
        if (!t) return;
        const std::string h = t->hex();
        CHECK(h.size() == kTokenBytes * 2);
        CHECK(seen.insert(h).second);  // never repeats
        auto back = Token::from_hex(h);
        CHECK(back && back->equals(*t));
    }
    CHECK(!Token::from_hex("abc"));
    CHECK(!Token::from_hex(std::string(kTokenBytes * 2, 'g')));
    // The CSPRNG fills everything it is asked for.
    std::vector<uint8_t> buf(4096, 0);
    CHECK(random_bytes(buf.data(), buf.size(), &err));
    size_t zeros = 0;
    for (uint8_t b : buf) zeros += b == 0;
    CHECK(zeros < 64);  // ~16 expected
}

void bodies() {
    check::phase("bodies");
    Grant g{42, *Token::generate()};
    wire::Writer w;
    g.write(w);
    Grant g2;
    wire::Reader r(w.data());
    CHECK(g2.read(r) && r.done());
    CHECK(g2.session == 42 && g2.token.equals(g.token));

    Join j{42, g.token, "input"};
    wire::Writer jw;
    j.write(jw);
    Join j2;
    wire::Reader jr(jw.data());
    CHECK(j2.read(jr) && jr.done());
    CHECK(j2.session == 42 && j2.token.equals(g.token) && j2.lane == "input");

    // Truncated bodies fail.
    std::string cut = jw.data().substr(0, 10);
    wire::Reader cr(cut);
    Join j3;
    CHECK(!j3.read(cr));
    CHECK(valid_lane_name("input") && valid_lane_name("a.b_c-9"));
    CHECK(!valid_lane_name("") && !valid_lane_name("in put") && !valid_lane_name(std::string(65, 'a')));
}

void registry() {
    check::phase("registry");
    Registry reg;
    auto g = reg.open(1);
    CHECK(g.has_value());
    if (!g) return;
    CHECK(!reg.open(1));  // already in a session
    auto g2 = reg.open(2);
    CHECK(g2 && g2->session != g->session && !g2->token.equals(g->token));
    CHECK(reg.sessions() == 2);
    const Registry::Member* m = reg.member(1);
    CHECK(m && m->control() && m->session == g->session);
    CHECK(reg.control(g->session) == ConnId(1));

    // Wrong session, wrong token (another session's), bad name.
    CHECK(reg.join(10, Join{999, g->token, "input"}) == JoinResult::NoSession);
    CHECK(reg.join(10, Join{g->session, g2->token, "input"}) == JoinResult::BadToken);
    CHECK(reg.join(10, Join{g->session, g->token, "no way"}) == JoinResult::BadLane);
    CHECK(!reg.member(10));

    CHECK(reg.join(10, Join{g->session, g->token, "input"}) == JoinResult::Joined);
    CHECK(reg.join(10, Join{g->session, g->token, "other"}) == JoinResult::Already);
    CHECK(reg.join(11, Join{g->session, g->token, "input"}) == JoinResult::LaneTaken);
    CHECK(reg.join(12, Join{g->session, g->token, "audio"}) == JoinResult::Joined);
    CHECK(reg.lane(g->session, "input") == ConnId(10));
    CHECK(reg.lanes(g->session).size() == 2);
    m = reg.member(10);
    CHECK(m && !m->control() && m->lane == "input");

    // A lane closing ends only itself, and its name stays used.
    CHECK(reg.closed(10).empty());
    CHECK(!reg.lane(g->session, "input"));
    CHECK(reg.join(13, Join{g->session, g->token, "input"}) == JoinResult::LaneTaken);

    // The control closing ends the session and hands back its lanes.
    auto close = reg.closed(1);
    CHECK(close.size() == 1 && close[0] == 12);
    CHECK(!reg.member(12) && reg.sessions() == 1);
    CHECK(reg.join(14, Join{g->session, g->token, "fresh"}) == JoinResult::NoSession);
    CHECK(reg.closed(12).empty());  // already gone: harmless
    CHECK(reg.closed(999).empty());
}

}  // namespace

int main() {
    check::start_watchdog("test_lanes");
    tokens();
    bodies();
    registry();
    return check::finish("test_lanes");
}
