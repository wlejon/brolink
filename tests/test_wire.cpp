// Framing primitives, the message splitter and Windows command-line quoting.
#include "brolink/lanes.h"
#include "brolink/stream.h"
#include "brolink/wire.h"
#include "check.h"

#include <cstring>
#include <limits>

using namespace brolink;

namespace {

void primitives() {
    check::phase("primitives");
    wire::Writer w;
    w.u8(0xAB);
    w.u16(0x1234);
    w.u32(0xDEADBEEF);
    w.u64(0x0102030405060708ull);
    w.varint(0);
    w.varint(300);
    w.varint(~0ull);
    w.svarint(-1);
    w.svarint(INT64_MIN);
    w.f32(1.5f);
    w.boolean(true);
    w.str("hello");
    const uint8_t b[3] = {1, 2, 3};
    w.bytes(b, 3);
    w.strings({"a", "bc"});
    w.pairs({{"k", "v"}});
    // Little endian on the wire, whatever the host.
    CHECK(std::memcmp(w.data().data() + 1, "\x34\x12\xEF\xBE\xAD\xDE", 6) == 0);

    wire::Reader r(w.data());
    CHECK(r.u8() == 0xAB);
    CHECK(r.u16() == 0x1234);
    CHECK(r.u32() == 0xDEADBEEF);
    CHECK(r.u64() == 0x0102030405060708ull);
    CHECK(r.varint() == 0);
    CHECK(r.varint() == 300);
    CHECK(r.varint() == ~0ull);
    CHECK(r.svarint() == -1);
    CHECK(r.svarint() == INT64_MIN);
    CHECK(r.f32() == 1.5f);
    CHECK(r.boolean());
    CHECK(r.str() == "hello");
    CHECK((r.bytes() == std::vector<uint8_t>{1, 2, 3}));
    CHECK((r.strings() == std::vector<std::string>{"a", "bc"}));
    auto p = r.pairs();
    CHECK(p.size() == 1 && p[0].first == "k" && p[0].second == "v");
    CHECK(r.done());

    // Bounds: nothing reads past the end, and failure sticks.
    wire::Reader short_read(std::string_view("\x05" "ab", 3));
    CHECK(short_read.str().empty());
    CHECK(!short_read.ok());
    wire::Reader overflow(std::string_view("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x02", 10));
    overflow.varint();
    CHECK(!overflow.ok());
    wire::Reader limited(std::string_view("\x05" "abcde", 6));
    CHECK(limited.str_max(4).empty() && !limited.ok());
    wire::Reader count(std::string_view("\x7F", 1));
    CHECK(count.count() == 0 && !count.ok());
    wire::Writer big;
    big.svarint(int64_t(INT32_MAX) + 1);
    wire::Reader s32(big.data());
    s32.svarint32();
    CHECK(!s32.ok());
}

void splitter() {
    check::phase("splitter");
    std::string stream = wire::make_message(7, "abc") + wire::make_message(8, "") + wire::make_message(9, "xyz");
    // Byte at a time: messages come out whole and in order.
    wire::MessageSplitter sp;
    std::vector<std::pair<uint16_t, std::string>> got;
    for (char c : stream) {
        sp.feed(&c, 1);
        wire::MessageSplitter::Message m;
        while (sp.next(m)) got.emplace_back(m.type, std::string(m.payload));
    }
    CHECK(got.size() == 3);
    CHECK(got.size() == 3 && got[0].first == 7 && got[0].second == "abc" && got[1].second.empty() &&
          got[2].second == "xyz");
    CHECK(!sp.error() && sp.buffered() == 0);

    // A length over the splitter's own maximum is a framing error.
    wire::MessageSplitter small(16);
    std::string big = wire::make_message(1, std::string(15, 'x'));
    small.feed(big.data(), big.size());
    wire::MessageSplitter::Message m;
    CHECK(!small.next(m) && small.error());
    wire::MessageSplitter ok16(17);
    ok16.feed(big.data(), big.size());
    CHECK(ok16.next(m) && m.payload.size() == 15);
    for (uint32_t bad : {0u, 1u}) {
        wire::MessageSplitter s;
        char hdr[4] = {char(bad), 0, 0, 0};
        s.feed(hdr, 4);
        CHECK(!s.next(m) && s.error());
    }
}

void command_line() {
    check::phase("command line");
    CHECK(windows_command_line({"cmd.exe"}) == "cmd.exe");
    CHECK(windows_command_line({"C:\\Program Files\\x.exe", "-a", "b c"}) == "\"C:\\Program Files\\x.exe\" -a \"b c\"");
    CHECK(windows_command_line({"x", "a\"b"}) == "x \"a\\\"b\"");
    CHECK(windows_command_line({"x", "trail\\ space\\"}) == "x \"trail\\ space\\\\\"");
    CHECK(windows_command_line({"x", ""}) == "x \"\"");
    CHECK(windows_command_line({"C:\\dir\\x.exe", "C:\\no\\quote"}) == "C:\\dir\\x.exe C:\\no\\quote");
}

void symbolic_and_adversarial_verification() {
    check::phase("symbolic bitvector & adversarial verification");

    // 1. Symbolic bitvector equivalence for 64-bit unsigned LEB128 and signed zigzag varints
    // across all bit-width boundaries and wraparound points.
    const uint64_t u64_boundaries[] = {
        0ull,
        1ull,
        0x7Full,
        0x80ull,
        0x3FFFull,
        0x4000ull,
        0x1FFFFFull,
        0x200000ull,
        0xFFFFFFFull,
        0x10000000ull,
        0x7FFFFFFFull,
        0x80000000ull,
        0xFFFFFFFFull,
        0x100000000ull,
        0x7FFFFFFFFFFFFFFFull,
        0x8000000000000000ull,
        0xFFFFFFFFFFFFFFFEull,
        0xFFFFFFFFFFFFFFFFull,
    };
    for (uint64_t v : u64_boundaries) {
        wire::Writer w;
        w.varint(v);
        CHECK(w.size() >= 1 && w.size() <= 10);
        // Every prefix [0 .. w.size()-1] is truncated and must fail cleanly without OOB read.
        for (size_t cut = 0; cut < w.size(); ++cut) {
            wire::Reader tr(std::string_view(w.data().data(), cut));
            (void)tr.varint();
            CHECK(!tr.ok() && !tr.done());
        }
        wire::Reader r(w.data());
        CHECK(r.varint() == v && r.done());
    }

    const int64_t s64_boundaries[] = {
        0, 1, -1, 63, -64, 64, -65,
        INT32_MIN, INT32_MAX,
        int64_t(INT32_MIN) - 1, int64_t(INT32_MAX) + 1,
        INT64_MIN, INT64_MAX,
        INT64_MIN + 1, INT64_MAX - 1,
    };
    for (int64_t sv : s64_boundaries) {
        wire::Writer w;
        w.svarint(sv);
        wire::Reader r(w.data());
        CHECK(r.svarint() == sv && r.done());

        wire::Reader r32(w.data());
        int32_t v32 = r32.svarint32();
        if (sv >= INT32_MIN && sv <= INT32_MAX) {
            CHECK(r32.done() && v32 == int32_t(sv));
        } else {
            CHECK(!r32.ok() && v32 == 0);
        }
    }

    // 2. Exhaustive 10th-byte LEB128 bitvector boundary: after 9 continuation bytes (63 bits),
    // the 10th byte may only carry bit 0 (0x00 or 0x01). Any byte >= 0x02 (either > 64 bits or
    // continuation bit 0x80 set on byte 10) must be rejected as 64-bit overflow.
    for (int b10 = 0; b10 <= 255; ++b10) {
        char buf[10];
        std::memset(buf, 0xFF, 9);
        buf[9] = char(uint8_t(b10));
        wire::Reader r(std::string_view(buf, 10));
        uint64_t val = r.varint();
        if (b10 == 0) {
            CHECK(r.done() && val == 0x7FFFFFFFFFFFFFFFull);
        } else if (b10 == 1) {
            CHECK(r.done() && val == 0xFFFFFFFFFFFFFFFFull);
        } else {
            CHECK(!r.ok() && val == 0);
        }
    }

    // 3. Adversarial 64-bit SIZE_MAX / UINT64_MAX / UINT32_MAX lengths in str, bytes, raw, count,
    // strings, pairs: must never wrap pos_ + n or reserve() unbounded memory.
    for (uint64_t adv_len : {uint64_t(UINT32_MAX), uint64_t(UINT32_MAX) + 1,
                             uint64_t(INT64_MAX), uint64_t(UINT64_MAX) - 4, uint64_t(UINT64_MAX)}) {
        wire::Writer w;
        w.varint(adv_len);
        w.u32(0x11223344u);  // trailing bytes so remaining() > 0

        {
            wire::Reader r(w.data());
            CHECK(r.str().empty() && !r.ok());
        }
        {
            wire::Reader r(w.data());
            CHECK(r.bytes().empty() && !r.ok());
        }
        {
            wire::Reader r(w.data());
            CHECK(r.count(0) == 0 && !r.ok());
        }
        {
            wire::Reader r(w.data());
            CHECK(r.count(1) == 0 && !r.ok());
        }
        {
            wire::Reader r(w.data());
            CHECK(r.strings().empty() && !r.ok());
        }
        {
            wire::Reader r(w.data());
            CHECK(r.pairs().empty() && !r.ok());
        }
        {
            wire::Reader r(w.data());
            CHECK(r.raw(size_t(adv_len)).empty() && !r.ok());
        }
    }

    // 4. Truncated frames at every byte offset [0 .. frame.size()-1] for MessageSplitter,
    // Grant, and Join control frames.
    std::string framed = wire::make_message(0x4210, "payload_bytes_12345");
    for (size_t cut = 0; cut < framed.size(); ++cut) {
        wire::MessageSplitter sp;
        sp.feed(framed.data(), cut);
        wire::MessageSplitter::Message m;
        CHECK(!sp.next(m));
        CHECK(!sp.error());
        // Feeding the remaining suffix completes the exact message.
        sp.feed(framed.data() + cut, framed.size() - cut);
        CHECK(sp.next(m) && m.type == 0x4210 && m.payload == "payload_bytes_12345");
        CHECK(!sp.error() && sp.buffered() == 0);
    }

    // Adversarial uint32_t frame lengths (0, 1, max+1, UINT32_MAX) rejected deterministically.
    for (uint32_t bad_len : {0u, 1u, uint32_t(wire::kDefaultMaxMessage + 1), 0x80000000u, 0xFFFFFFFFu}) {
        wire::MessageSplitter sp;
        char hdr[6] = {
            char(bad_len), char(bad_len >> 8), char(bad_len >> 16), char(bad_len >> 24),
            0x01, 0x00,
        };
        sp.feed(hdr, sizeof hdr);
        wire::MessageSplitter::Message m;
        CHECK(!sp.next(m) && sp.error());
    }

    // Grant & Join control frames truncated at every byte offset [0 .. size-1].
    lanes::Token tok{};
    for (size_t i = 0; i < lanes::kTokenBytes; ++i) tok.bytes[i] = uint8_t(i + 1);
    lanes::Grant grant{0x123456789ABCDEF0ull, tok};
    wire::Writer gw;
    grant.write(gw);
    for (size_t cut = 0; cut < gw.size(); ++cut) {
        wire::Reader r(std::string_view(gw.data().data(), cut));
        lanes::Grant g_out;
        CHECK(!g_out.read(r) || !r.done());
    }

    lanes::Join join{0x123456789ABCDEF0ull, tok, "video-lane.0"};
    wire::Writer jw;
    join.write(jw);
    for (size_t cut = 0; cut < jw.size(); ++cut) {
        wire::Reader r(std::string_view(jw.data().data(), cut));
        lanes::Join j_out;
        CHECK(!j_out.read(r) || !r.done());
    }
}

}  // namespace

int main() {
    check::start_watchdog("test_wire");
    primitives();
    splitter();
    command_line();
    symbolic_and_adversarial_verification();
    return check::finish("test_wire");
}
