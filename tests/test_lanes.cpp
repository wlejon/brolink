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

void model_check_lanes_and_hol_isolation() {
    check::phase("exhaustive lane registry & HOL-blocking isolation model check");

    // 1. Exhaustive state-space model check over all 5! = 120 action orderings of
    //    session lifecycle transitions (join input, join video, replay input, close input, close control)
    //    interleaved with a second concurrent session.
    enum Action {
        kJoinInput = 0,
        kJoinVideo = 1,
        kReplayInput = 2,
        kCloseInput = 3,
        kCloseControl = 4,
    };
    std::vector<Action> perm = {kJoinInput, kJoinVideo, kReplayInput, kCloseInput, kCloseControl};
    int schedules = 0;
    do {
        ++schedules;
        Registry reg;
        auto g1 = reg.open(100);
        auto g2 = reg.open(200);
        CHECK(g1.has_value() && g2.has_value());
        CHECK(reg.join(201, Join{g2->session, g2->token, "input"}) == JoinResult::Joined);

        bool s1Alive = true;
        bool inputUsed = false;
        bool inputLive = false;
        bool videoLive = false;

        for (Action act : perm) {
            switch (act) {
                case kJoinInput: {
                    JoinResult r = reg.join(101, Join{g1->session, g1->token, "input"});
                    if (!s1Alive) {
                        CHECK(r == JoinResult::NoSession);
                    } else if (inputLive) {
                        CHECK(r == JoinResult::Already);
                    } else if (inputUsed) {
                        CHECK(r == JoinResult::LaneTaken);
                    } else {
                        CHECK(r == JoinResult::Joined);
                        inputUsed = true;
                        inputLive = true;
                    }
                    break;
                }
                case kJoinVideo: {
                    JoinResult r = reg.join(102, Join{g1->session, g1->token, "video"});
                    if (!s1Alive) {
                        CHECK(r == JoinResult::NoSession);
                    } else {
                        CHECK(r == JoinResult::Joined);
                        videoLive = true;
                    }
                    break;
                }
                case kReplayInput: {
                    JoinResult r = reg.join(103, Join{g1->session, g1->token, "input"});
                    if (!s1Alive) {
                        CHECK(r == JoinResult::NoSession);
                    } else if (inputUsed) {
                        CHECK(r == JoinResult::LaneTaken);
                    } else {
                        CHECK(r == JoinResult::Joined);
                        inputUsed = true;
                        // Connection 103 took "input" before 101 tried
                        auto closed = reg.closed(103);
                        CHECK(closed.empty());
                    }
                    break;
                }
                case kCloseInput: {
                    auto closed = reg.closed(101);
                    CHECK(closed.empty());
                    inputLive = false;
                    break;
                }
                case kCloseControl: {
                    auto closed = reg.closed(100);
                    size_t expected = (inputLive ? 1u : 0u) + (videoLive ? 1u : 0u);
                    CHECK(closed.size() == expected);
                    s1Alive = false;
                    inputLive = false;
                    videoLive = false;
                    break;
                }
            }

            // Relational invariants at every state:
            // Session 2 must remain completely unaffected by any transition in Session 1.
            CHECK(reg.control(g2->session) == ConnId(200));
            CHECK(reg.lane(g2->session, "input") == ConnId(201));
            CHECK(reg.sessions() == (s1Alive ? 2u : 1u));
            if (s1Alive) {
                CHECK(reg.control(g1->session) == ConnId(100));
                CHECK(reg.lane(g1->session, "input") == (inputLive ? std::optional<ConnId>(101) : std::nullopt));
                CHECK(reg.lane(g1->session, "video") == (videoLive ? std::optional<ConnId>(102) : std::nullopt));
            } else {
                CHECK(!reg.control(g1->session).has_value());
                CHECK(!reg.member(100) && !reg.member(101) && !reg.member(102));
            }
        }
    } while (std::next_permutation(perm.begin(), perm.end()));
    CHECK(schedules == 120);

    // 2. Multi-lane HOL-blocking isolation: a stalled partial bulk frame on the
    //    `video` lane never blocks or corrupts interleaved 1-byte / chunked
    //    frames on the `input` and `control` lanes.
    wire::MessageSplitter controlSp, inputSp, videoSp;
    std::string videoFrame = wire::make_message(0x30, std::string(4096, 'V'));
    // Feed only the first 19 bytes of the 4KB video frame (stalled mid-frame)
    videoSp.feed(videoFrame.data(), 19);
    wire::MessageSplitter::Message msg;
    CHECK(!videoSp.next(msg) && !videoSp.error());

    for (uint16_t seq = 1; seq <= 32; ++seq) {
        std::string inMsg = wire::make_message(seq, "key:" + std::to_string(seq));
        std::string ctlMsg = wire::make_message(uint16_t(0x100 + seq), "ping");
        // Interleave 1 byte at a time across input, control, and 1 byte of stalled video
        size_t maxLen = std::max(inMsg.size(), ctlMsg.size());
        for (size_t i = 0; i < maxLen; ++i) {
            if (i < inMsg.size()) inputSp.feed(&inMsg[i], 1);
            if (i < ctlMsg.size()) controlSp.feed(&ctlMsg[i], 1);
        }
        CHECK(inputSp.next(msg) && msg.type == seq && msg.payload == "key:" + std::to_string(seq));
        CHECK(controlSp.next(msg) && msg.type == uint16_t(0x100 + seq) && msg.payload == "ping");
        CHECK(!videoSp.next(msg) && !videoSp.error());
    }
    // Finally complete the stalled video frame and verify it delivers intact.
    videoSp.feed(videoFrame.data() + 19, videoFrame.size() - 19);
    CHECK(videoSp.next(msg) && msg.type == 0x30 && msg.payload.size() == 4096);
    CHECK(!videoSp.error() && videoSp.buffered() == 0);
}

}  // namespace

int main() {
    check::start_watchdog("test_lanes");
    tokens();
    bodies();
    registry();
    model_check_lanes_and_hol_isolation();
    return check::finish("test_lanes");
}
