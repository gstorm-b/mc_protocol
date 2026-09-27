#include "doctest/doctest.h"

#include "mc/core/log.h"

#include <string>
#include <string_view>

using mc::LogLevel;
using mc::LogSink;
using mc::NullLogSink;

namespace {

// A sink whose enabled() result is set by the test, and which records every write() call so the
// test can tell whether write() (and, by construction of logIfEnabled() below, the message that
// would have been passed to it) happened at all.
class CountingSink : public LogSink {
public:
    bool enabledResult = false;
    mutable size_t enabledCalls = 0;
    size_t writeCalls = 0;
    LogLevel lastLevel{};
    std::string lastCategory;
    std::string lastMessage;

    bool enabled(LogLevel /*level*/) const noexcept override {
        ++enabledCalls;
        return enabledResult;
    }

    void write(LogLevel level, std::string_view category,
               std::string_view message) noexcept override {
        ++writeCalls;
        lastLevel = level;
        lastCategory = category; // copied out; the header's contract says message/category are
        lastMessage = std::string(message); // only valid during this call.
    }
};

// The mandated call-site pattern from log.h's LogSink doc comment: check enabled() first, and
// only build (here: increment messageBuilds, standing in for "build message in a stack buffer")
// and send a message when it says yes.
void logIfEnabled(LogSink& sink, LogLevel level, size_t& messageBuilds) {
    if (sink.enabled(level)) {
        ++messageBuilds;
        sink.write(level, "mc.test", "a message");
    }
}

} // namespace

TEST_CASE("LOG-01 disabled sink: write() is never called and no message is built") {
    CountingSink sink; // enabledResult defaults to false.
    size_t messageBuilds = 0;

    logIfEnabled(sink, LogLevel::Error, messageBuilds);

    CHECK(sink.enabledCalls == 1);
    CHECK(sink.writeCalls == 0);
    CHECK(messageBuilds == 0);
}

TEST_CASE("LOG-01 enabled sink: write() is called with the message that was built") {
    CountingSink sink;
    sink.enabledResult = true;
    size_t messageBuilds = 0;

    logIfEnabled(sink, LogLevel::Warn, messageBuilds);

    CHECK(sink.writeCalls == 1);
    CHECK(messageBuilds == 1);
    CHECK(sink.lastLevel == LogLevel::Warn);
    CHECK(sink.lastCategory == "mc.test");
    CHECK(sink.lastMessage == "a message");
}

TEST_CASE("LOG-01 NullLogSink disables every level and never calls write") {
    NullLogSink sink;
    LogLevel levels[] = {LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
                          LogLevel::Warn,  LogLevel::Error, LogLevel::Off};
    for (LogLevel level : levels) {
        CHECK_FALSE(sink.enabled(level));
    }
    size_t messageBuilds = 0;
    logIfEnabled(sink, LogLevel::Error, messageBuilds);
    CHECK(messageBuilds == 0);
}

TEST_CASE("NullLogSink::write() is a harmless no-op (Checkpoint A coverage gap)") {
    // write() is never reached through logIfEnabled() (enabled() always says no, by design), so
    // nothing else in this file ever calls it; call it directly to prove the no-op body itself
    // does nothing harmful, matching its doc comment ("No-op: never reached...").
    NullLogSink sink;
    sink.write(LogLevel::Error, "mc.test", "unreachable in practice, but must not crash");
}

TEST_CASE("LOG-02 hexDump renders the module spec's own example") {
    // include/mc/core/log.h's own doc comment: "50 00 00 FF FF 03 00 ...".
    uint8_t bytes[] = {0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00};
    mc::ByteView view{bytes, sizeof(bytes)};

    char plain[32];
    size_t plainLen = mc::hexDump(view, plain, sizeof(plain));
    CHECK(plainLen == 20); // 7 bytes * 2 hex digits + 6 separating spaces.
    CHECK(std::string_view(plain, plainLen) == "50 00 00 FF FF 03 00");

    // The same bytes with names=true: byte 0x03 is ETX and renders as "<ETX>" instead of "03".
    char named[32];
    size_t namedLen = mc::hexDump(view, named, sizeof(named), true);
    CHECK(std::string_view(named, namedLen) == "50 00 00 FF FF <ETX> 00");
}

TEST_CASE("LOG-02 hexDump renders every spec section 8.6 control code by name") {
    uint8_t bytes[] = {0x02, 0x03, 0x05, 0x06, 0x15, 0x0D, 0x0A, 0x10, 0x41};
    mc::ByteView view{bytes, sizeof(bytes)};

    char out[64];
    size_t len = mc::hexDump(view, out, sizeof(out), true);
    CHECK(std::string_view(out, len) ==
          "<STX> <ETX> <ENQ> <ACK> <NAK> <CR> <LF> <DLE> 41");
}

TEST_CASE("LOG-02 hexDump is snprintf-like: returns the full length, truncates to capacity") {
    uint8_t bytes[] = {0xAB, 0xCD};
    mc::ByteView view{bytes, sizeof(bytes)};

    char small[3]; // room for "AB" + NUL, not the full "AB CD".
    size_t len = mc::hexDump(view, small, sizeof(small));
    CHECK(len == 5); // the full length "AB CD" would need, regardless of what fit.
    CHECK(std::string_view(small) == "AB");

    CHECK(mc::hexDump(view, nullptr, 0) == 5); // capacity 0: nothing written, same full length.
}
