// tests/core/session/harness.h (spec SPEC-core-session.md, Testing Strategy): FakeClock, a 3E
// ScriptedPeer (Binary and ASCII), OutputRecorder, RecordingLogSink. Shared by every
// core-session test binary's test_session_*.cpp file from T-022 onward.
#pragma once

#include "mc/core/frame_config.h"
#include "mc/core/log.h"
#include "mc/core/request.h"
#include "mc/core/session.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mc::test {

/// A fake, test-driven monotonic clock: the engine never reads a clock itself (spec), so every
/// test advances this by hand and passes `now()` to whichever `Session` input it calls next.
class FakeClock {
public:
    TimeMs now() const noexcept { return m_now; }

    /// Advances the clock and returns the new time.
    TimeMs advance(TimeMs deltaMs) noexcept {
        m_now += deltaMs;
        return m_now;
    }

    void set(TimeMs t) noexcept { m_now = t; }

private:
    TimeMs m_now{0};
};

/// What `ScriptedPeer::respond()` answers the next request with. "ValueStore-independent" (the
/// module spec's own wording): the test hands this directly, with no real memory image behind it.
struct PeerScript {
    enum class Kind : uint8_t {
        Ok,      ///< A successful response; `words`/`bits` supply the read data (ignored for a
                 ///< write, which always answers with no data).
        PlcError ///< A PLC error response; `plcEndCode` is the end code (never 0).
    };

    Kind kind{Kind::Ok};
    uint16_t plcEndCode{0};       ///< `Kind::PlcError` only.
    std::vector<uint16_t> words;  ///< `Kind::Ok`, word device: value per requested word, index 0
                                   ///< = `request.head`. Short reads pad with 0.
    std::vector<uint8_t> bits;    ///< `Kind::Ok`, bit device: 0/1 value per requested point, same
                                   ///< indexing. Short reads pad with 0.
};

/// Builds 3E Binary/ASCII response frames for a `Request`, by hand, from spec section 5.1's own
/// byte layout (not by reusing `mc::detail`'s frame code, which test binaries cannot reach): the
/// harness's whole point is to drive `Session` without a real PLC.
class ScriptedPeer {
public:
    explicit ScriptedPeer(FrameConfig cfg) : m_cfg(cfg) {}

    /// @return The wire bytes of one complete response frame to `r`, per `script`.
    std::vector<uint8_t> respond(const Request& r, const PeerScript& script) const;

private:
    FrameConfig m_cfg;
};

/// The bytes of record `id` of `tests/vectors/<file>` (spec "Testing Strategy": serial behaviour
/// uses the golden vectors directly, e.g. `3c_f1.vec` / `V-3C1-01`). Throws when the file or the
/// record is missing, which doctest reports as a failed test.
std::vector<uint8_t> vectorBytes(const char* file, std::string_view id);

/// A 3C Format 1 response to a word read, built by hand from spec section 5.5 and 2.5, not by the
/// library: STX, `F90000FF00` (frame ID, station, network, PC, self-station of the default
/// route), 4 upper-case hex characters per word, ETX, then the sum check (the low byte of the sum
/// of every byte after STX through ETX, 2 hex characters). Equals V-3C1-02 for 1995H, 1202H, 1130H
/// (a test in test_session_fault.cpp checks that), and lets a test make a response of any length.
std::vector<uint8_t> serialReadResponse3C(const std::vector<uint16_t>& words);

/// A single previously-popped `Output`, with every raw-pointer view (`bytes`, `payload`,
/// `changes`, `chunks`) copied into owned storage so it stays valid long after the `Session` call
/// that produced it (`Output`'s own contract: those views are only valid until the *next* input
/// call, which `OutputRecorder::drain()` deliberately outlives).
struct RecordedOutput {
    OutputKind kind{OutputKind::Send};
    std::vector<uint8_t> bytes;
    DeviceType deviceType{DeviceType::D};
    uint32_t round{0};
    std::vector<Change> changes;
    std::vector<ChunkInfo> chunks;
    CycleInfo cycle{};
    RequestId requestId{0};
    Error error{};
    std::vector<uint8_t> payload;
    LinkFaultKind fault{LinkFaultKind::Timeout};
    bool reopenTransport{false};
};

/// Drains a `Session` completely (the drain contract) after each input call and keeps every
/// output ever seen, so a test can inspect the whole sequence afterwards instead of asserting at
/// each call site.
class OutputRecorder {
public:
    /// Pops every pending output from `session` and appends it.
    void drain(Session& session);

    size_t size() const noexcept { return m_items.size(); }
    bool empty() const noexcept { return m_items.empty(); }
    const RecordedOutput& operator[](size_t i) const noexcept { return m_items[i]; }
    const RecordedOutput& back() const noexcept { return m_items.back(); }
    const std::vector<RecordedOutput>& all() const noexcept { return m_items; }
    void clear() noexcept { m_items.clear(); }

private:
    std::vector<RecordedOutput> m_items;
};

/// A `LogSink` that records every call instead of discarding it, so a test can assert on what
/// `Session` logged (e.g. SES-26's "clock going backwards ... logged at Warn").
class RecordingLogSink : public LogSink {
public:
    struct Entry {
        LogLevel level;
        std::string category;
        std::string message;
    };

    bool enabled(LogLevel level) const noexcept override { return level >= m_minLevel; }

    void write(LogLevel level, std::string_view category,
               std::string_view message) noexcept override {
        m_entries.push_back(Entry{level, std::string(category), std::string(message)});
    }

    /// Only levels at or above `level` are recorded from now on (`enabled()`'s own contract);
    /// default is every level.
    void setMinLevel(LogLevel level) noexcept { m_minLevel = level; }

    const std::vector<Entry>& entries() const noexcept { return m_entries; }
    bool any(LogLevel level) const noexcept {
        for (const Entry& e : m_entries) {
            if (e.level == level) {
                return true;
            }
        }
        return false;
    }
    void clear() noexcept { m_entries.clear(); }

private:
    LogLevel m_minLevel{LogLevel::Trace};
    std::vector<Entry> m_entries;
};

} // namespace mc::test
