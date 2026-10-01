#include "rig.h"

#include "doctest/doctest.h"

#include "mc/core/device.h"

#include <utility>

namespace mc::test {
namespace {

FrameConfig withRigDefaults(FrameConfig frame) {
    frame.readRetries = 2; // the FrameConfig default (0) would hide retries
    return frame;
}

Session makeSession(const FrameConfig& frame, SessionConfig cfg) {
    cfg.maxConsecutiveLinkErrors = 3;
    Expected<Session> created = Session::create(frame, cfg);
    REQUIRE(created.hasValue());
    return std::move(created.value());
}

} // namespace

const std::vector<Combo>& combos() {
    static const std::vector<Combo> list = {
        {"3E Binary", FrameConfig::frame3E(DataCode::Binary)},
        {"3E ASCII", FrameConfig::frame3E(DataCode::Ascii)},
        {"1E Binary", FrameConfig::frame1E(DataCode::Binary)},
        {"1E ASCII", FrameConfig::frame1E(DataCode::Ascii)},
        {"3C F1", FrameConfig::frame3C(SerialFormat::Format1)},
        {"3C F2", FrameConfig::frame3C(SerialFormat::Format2)},
        {"3C F3", FrameConfig::frame3C(SerialFormat::Format3)},
        {"3C F4", FrameConfig::frame3C(SerialFormat::Format4)},
        {"1C F1", FrameConfig::frame1C(SerialFormat::Format1)},
        {"1C F2", FrameConfig::frame1C(SerialFormat::Format2)},
        {"1C F3", FrameConfig::frame1C(SerialFormat::Format3)},
        {"1C F4", FrameConfig::frame1C(SerialFormat::Format4)},
    };
    return list;
}

Rig::Rig(const Combo& combo, const SessionConfig& session, uint32_t seed)
    : m_frame(withRigDefaults(combo.frame)),
      m_session(makeSession(m_frame, session)),
      m_mock(m_frame),
      m_toMock(seed),
      m_toSession(seed ^ 0x9E3779B9u) {}

void Rig::linkUp() {
    m_session.linkUp(m_now);
    settle();
}

void Rig::linkDown() {
    m_session.linkDown(m_now);
    settle();
}

Expected<RequestId> Rig::submit(const Request& r) {
    Expected<RequestId> id = m_session.submit(r, m_now);
    settle();
    return id;
}

void Rig::tick() {
    m_session.tick(m_now);
    settle();
}

bool Rig::jumpToDeadline() {
    const TimeMs deadline = m_session.nextDeadline();
    if (deadline == kNoDeadline) {
        return false;
    }
    if (deadline > m_now) {
        m_now = deadline;
    }
    tick();
    return true;
}

bool Rig::runRound() {
    const size_t before = cycleCount();
    if (!jumpToDeadline()) {
        return false;
    }
    return cycleCount() > before;
}

size_t Rig::cycleCount() const {
    size_t n = 0;
    for (const Event& e : m_events) {
        if (e.kind == OutputKind::CycleDone) {
            ++n;
        }
    }
    return n;
}

std::vector<Event> Rig::requestDones(RequestId id) const {
    std::vector<Event> found;
    for (const Event& e : m_events) {
        if (e.kind == OutputKind::RequestDone && e.requestId == id) {
            found.push_back(e);
        }
    }
    return found;
}

std::vector<Event> Rig::ofKind(OutputKind kind) const {
    std::vector<Event> found;
    for (const Event& e : m_events) {
        if (e.kind == kind) {
            found.push_back(e);
        }
    }
    return found;
}

// Pops every pending output. Send goes on the wire to the mock; everything is recorded.
void Rig::drain() {
    Output out;
    while (m_session.nextOutput(out)) {
        Event e;
        e.kind = out.kind;
        switch (out.kind) {
        case OutputKind::Send:
            e.bytes.assign(out.bytes.data, out.bytes.data + out.bytes.size);
            m_toMock.write(out.bytes);
            break;
        case OutputKind::ValuesChanged:
            e.deviceType = out.deviceType;
            e.round = out.round;
            e.changes.assign(out.changes, out.changes + out.changeCount);
            break;
        case OutputKind::Snapshot:
            e.deviceType = out.deviceType;
            e.round = out.round;
            e.chunks.assign(out.chunks, out.chunks + out.chunkCount);
            break;
        case OutputKind::CycleDone:
            e.cycle = out.cycle;
            break;
        case OutputKind::RequestDone:
            e.requestId = out.requestId;
            e.error = out.error;
            e.payload.assign(out.payload.data, out.payload.data + out.payload.size);
            break;
        case OutputKind::LinkFault:
            e.fault = out.fault;
            e.error = out.error;
            e.reopenTransport = out.reopenTransport;
            break;
        }
        m_events.push_back(std::move(e));
    }
}

void Rig::settle() {
    drain();
    ByteBuf fragment;
    for (;;) {
        if (m_toMock.read(fragment)) {
            m_mock.bytesIn(view(fragment));
            ByteView response;
            while (m_mock.nextResponse(response)) {
                m_toSession.write(response);
            }
            continue;
        }
        if (m_toSession.read(fragment)) {
            m_session.bytesIn(view(fragment), m_now);
            drain();
            continue;
        }
        break;
    }
}

std::vector<Event> roundEvents(const std::vector<Event>& events, uint32_t round) {
    std::vector<Event> found;
    for (const Event& e : events) {
        const bool inRound =
            ((e.kind == OutputKind::ValuesChanged || e.kind == OutputKind::Snapshot) &&
             e.round == round) ||
            (e.kind == OutputKind::CycleDone && e.cycle.round == round);
        if (inRound) {
            found.push_back(e);
        }
    }
    return found;
}

std::vector<Change> allChanges(const std::vector<Event>& events) {
    std::vector<Change> found;
    for (const Event& e : events) {
        if (e.kind == OutputKind::ValuesChanged) {
            found.insert(found.end(), e.changes.begin(), e.changes.end());
        }
    }
    return found;
}

ByteBuf wordsLe(std::initializer_list<uint16_t> words) {
    ByteBuf bytes;
    for (uint16_t w : words) {
        bytes.push_back(static_cast<uint8_t>(w & 0xFFu));
        bytes.push_back(static_cast<uint8_t>(w >> 8));
    }
    return bytes;
}

Device dev(const char* text) {
    Expected<Device> d = parseDevice(text);
    REQUIRE(d.hasValue());
    return d.value();
}

} // namespace mc::test
