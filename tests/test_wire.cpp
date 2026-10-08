// Framing primitives, the message splitter and Windows command-line quoting.
#include "brolink/stream.h"
#include "brolink/wire.h"
#include "check.h"

#include <cstring>

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

}  // namespace

int main() {
    check::start_watchdog("test_wire");
    primitives();
    splitter();
    command_line();
    return check::finish("test_wire");
}
