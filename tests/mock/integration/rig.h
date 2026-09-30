// tests/mock/integration/rig.h (SPEC-mock-plc.md "Integration"): Session + MockPlc + fake clock +
// two seeded pipes, one call per scenario step. Every call that feeds the Session drains
// Session::nextOutput() completely (the drain contract), routes Send -> pipe -> MockPlc::bytesIn
// and MockPlc::nextResponse -> pipe -> Session::bytesIn until both pipes are empty, and records
// every output for assertions. Time is a number the rig owns: nothing sleeps, nothing reads a
// clock; a muted mock is a jump to Session::nextDeadline() followed by tick().
#pragma once

#include "pipe.h"

#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/session.h"
#include "mc/mock/mock_plc.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace mc::test {

/// One cell of the integration matrix: a name for failure messages and the frame the client and
/// the mock both speak. Later phases add 1E, 3C and 1C cells by extending combos(), not by
/// copying scenarios.
struct Combo {
    std::string name;
    FrameConfig frame;
};

/// The combinations every scenario runs on (v1 so far: 3E Binary and 3E ASCII).
const std::vector<Combo>& combos();

/// One Session output, with every view copied into owned storage so it outlives the input call
/// that produced it.
struct Event {
    OutputKind kind{OutputKind::Send};
    ByteBuf bytes;                 ///< Send
    DeviceType deviceType{DeviceType::D}; ///< ValuesChanged, Snapshot
    uint32_t round{0};             ///< ValuesChanged, Snapshot
    std::vector<Change> changes;   ///< ValuesChanged
    std::vector<ChunkInfo> chunks; ///< Snapshot
    CycleInfo cycle{};             ///< CycleDone
    RequestId requestId{0};        ///< RequestDone
    Error error{};                 ///< RequestDone, LinkFault
    ByteBuf payload;               ///< RequestDone
    LinkFaultKind fault{LinkFaultKind::Timeout}; ///< LinkFault
    bool reopenTransport{false};   ///< LinkFault
};

class Rig {
public:
    /// Rig defaults of the spec: readRetries = 2 on the frame, maxConsecutiveLinkErrors = 3.
    /// `session` may override the rest of the SessionConfig; `seed` fixes the fragmentation.
    explicit Rig(const Combo& combo, const SessionConfig& session = {}, uint32_t seed = 1);

    Session& session() noexcept { return m_session; }
    MockPlc& mock() noexcept { return m_mock; }
    const FrameConfig& frame() const noexcept { return m_frame; }
    TimeMs now() const noexcept { return m_now; }

    /// @name Inputs; each drains the Session and pumps both pipes until they are empty.
    /// @{
    void linkUp();
    void linkDown();
    Expected<RequestId> submit(const Request& r);
    /// tick() at the current time.
    void tick();
    /// Moves the clock forward by `ms`, without a tick.
    void advance(TimeMs ms) noexcept { m_now += ms; }
    /// Moves the clock to Session::nextDeadline() (never backwards) and ticks. This starts the
    /// next round when the Session is idle, and expires the response deadline when the mock is
    /// muted. Returns false, doing nothing, when there is no deadline (down or faulted).
    bool jumpToDeadline();
    /// Starts the next round (one jump to the deadline) and lets it run to its CycleDone.
    /// Returns false when the round did not complete (e.g. a fault or a muted mock).
    bool runRound();
    /// @}

    /// @name Observation
    /// @{
    const std::vector<Event>& events() const noexcept { return m_events; }
    void clearEvents() { m_events.clear(); }
    /// Number of CycleDone events recorded (since the last clearEvents()).
    size_t cycleCount() const;
    /// Every RequestDone event of `id`.
    std::vector<Event> requestDones(RequestId id) const;
    /// Every event of `kind`, in order.
    std::vector<Event> ofKind(OutputKind kind) const;
    /// Total fragments written to both pipes so far.
    size_t fragmentsWritten() const noexcept {
        return m_toMock.fragmentsWritten() + m_toSession.fragmentsWritten();
    }
    /// @}

private:
    void drain();
    /// Drains the Session, then moves bytes between the pipes until both are empty.
    void settle();

    FrameConfig m_frame;
    Session m_session;
    MockPlc m_mock;
    Pipe m_toMock;
    Pipe m_toSession;
    TimeMs m_now{0};
    std::vector<Event> m_events;
};

/// The events of `round` from `events`, in order: ValuesChanged and Snapshot by `round`,
/// CycleDone by `cycle.round`. Send, RequestDone and LinkFault events are not included.
std::vector<Event> roundEvents(const std::vector<Event>& events, uint32_t round);

/// Every Change of every ValuesChanged event in `events`, in order.
std::vector<Change> allChanges(const std::vector<Event>& events);

/// Little-endian payload bytes of `words`, the normalized layout of a word write.
ByteBuf wordsLe(std::initializer_list<uint16_t> words);

/// A ByteView over `bytes`.
inline ByteView view(const ByteBuf& bytes) noexcept { return ByteView{bytes.data(), bytes.size()}; }

/// The device `text` names; the text is a test literal, so a parse failure is a test bug.
Device dev(const char* text);

} // namespace mc::test
