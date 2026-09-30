// session_loop: drives the sans-I/O Session against the MockPlc, with no Qt and no socket.
//
// The Session never reads a clock, sleeps, or touches a transport: this program is the "event
// loop" that gives it bytes, time and a place to send its output. Here the transport is a pair of
// in-memory byte queues, the peer is a MockPlc, and time comes from std::chrono::steady_clock.
//
// What it shows, in order:
//   1. Round 1 after linkUp: nothing is reported while values are read for the first time, then
//      one snapshot per device type (in DeviceType order).
//   2. The program changes values inside the mock between rounds.
//   3. From round 2, a ValuesChanged event for exactly the points that changed, before that type's
//      snapshot.
// It runs three rounds and exits 0; it exits 1 if the link faults or the run takes too long.
#include "mc/core/device.h"
#include "mc/core/session.h"
#include "mc/mock/mock_plc.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kRounds = 3;
constexpr auto kGiveUpAfter = std::chrono::seconds(5);

// One direction of the in-memory transport.
using Wire = std::deque<uint8_t>;

std::string nameOf(mc::Device d) {
    char text[16];
    mc::formatDevice(d, text, sizeof text);
    return text;
}

mc::Device device(const char* text) { return mc::parseDevice(text).value(); }

void printSnapshot(const mc::Session& s, const mc::Output& out) {
    unsigned failed = 0;
    for (size_t i = 0; i < out.chunkCount; ++i) {
        if (out.chunks[i].state == mc::ChunkState::Failed) {
            ++failed;
        }
    }
    std::printf("round %u snapshot %s (%u failed chunks)\n", static_cast<unsigned>(out.round),
                mc::deviceInfo(out.deviceType).symbol, failed);
    const mc::ValueStore& values = s.values();
    for (size_t i = 0; i < values.segmentCount(out.deviceType); ++i) {
        const mc::SegmentView seg = values.segment(out.deviceType, i);
        std::printf("  %s x%u:", nameOf(seg.head).c_str(), static_cast<unsigned>(seg.count));
        for (uint32_t k = 0; k < seg.count; ++k) {
            const unsigned v = seg.words != nullptr ? seg.words[k] : seg.bits[k];
            std::printf(" %u", v);
        }
        std::printf("\n");
    }
}

void printChanges(const mc::Output& out) {
    for (size_t i = 0; i < out.changeCount; ++i) {
        const mc::Change& c = out.changes[i];
        std::printf("round %u changed %s: %u -> %u\n", static_cast<unsigned>(out.round),
                    nameOf(c.device).c_str(), static_cast<unsigned>(c.oldValue),
                    static_cast<unsigned>(c.newValue));
    }
}

} // namespace

int main() {
    const mc::FrameConfig frame = mc::FrameConfig::frame3E();

    // The peer: a mock PLC with some initial memory.
    mc::MockPlc plc(frame);
    plc.setWords(device("D2000"), {10, 20, 30, 40});
    plc.setBit(device("M2003"), true);
    plc.setBit(device("M2010"), true);
    plc.setBit(device("X2"), true);

    mc::SessionConfig cfg;
    cfg.cycleIntervalMs = 50;
    auto created = mc::Session::create(frame, cfg);
    if (!created) {
        std::printf("Session::create failed: %s\n", created.error().message);
        return 1;
    }
    mc::Session s = std::move(created.value());
    if (!s.subscribe(device("D2000"), 4) || !s.subscribe(device("M2000"), 16) ||
        !s.subscribe(device("X0"), 8)) {
        std::printf("subscribe failed\n");
        return 1;
    }

    const Clock::time_point start = Clock::now();
    const auto nowMs = [&]() -> mc::TimeMs {
        return static_cast<mc::TimeMs>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    };

    Wire toPlc;   // bytes the Session sent, on their way to the mock
    Wire toApp;   // bytes the mock answered, on their way back to the Session
    bool faulted = false;
    uint32_t roundsDone = 0;

    // The drain contract: after EVERY input call (linkUp, bytesIn, tick, ...) the output queue
    // must be emptied before the next input call. Everything below funnels through this.
    const auto drain = [&] {
        mc::Output out;
        while (s.nextOutput(out)) {
            switch (out.kind) {
            case mc::OutputKind::Send:
                toPlc.insert(toPlc.end(), out.bytes.data, out.bytes.data + out.bytes.size);
                break;
            case mc::OutputKind::ValuesChanged:
                printChanges(out);
                break;
            case mc::OutputKind::Snapshot:
                printSnapshot(s, out);
                break;
            case mc::OutputKind::CycleDone:
                roundsDone = out.cycle.round;
                break;
            case mc::OutputKind::RequestDone: // no ad-hoc requests in this example
                break;
            case mc::OutputKind::LinkFault: // the application decides; this one just stops
                faulted = true;
                break;
            }
        }
    };

    // Between rounds, change values inside the mock; the next round reports them.
    uint32_t changedAfterRound = 0;
    const auto changeMock = [&](uint32_t round) {
        if (round == 1) {
            std::printf("-- changing D2001 and M2005 in the mock\n");
            plc.setWord(device("D2001"), 21);
            plc.setBit(device("M2005"), true);
        } else if (round == 2) {
            std::printf("-- changing D2003 and X2 in the mock\n");
            plc.setWord(device("D2003"), 4000);
            plc.setBit(device("X2"), false);
        }
    };

    s.linkUp(nowMs());
    drain();
    while (!faulted && roundsDone < kRounds) {
        if (Clock::now() - start > kGiveUpAfter) {
            std::printf("gave up: the run took too long\n");
            return 1;
        }
        if (roundsDone != changedAfterRound) {
            changedAfterRound = roundsDone;
            changeMock(roundsDone);
        }

        // The mock is fed whatever the Session sent, in any fragmentation it likes.
        if (!toPlc.empty()) {
            const std::vector<uint8_t> request(toPlc.begin(), toPlc.end());
            toPlc.clear();
            plc.bytesIn(mc::ByteView{request.data(), request.size()});
            mc::ByteView response;
            while (plc.nextResponse(response)) {
                toApp.insert(toApp.end(), response.data, response.data + response.size);
            }
            continue;
        }
        if (!toApp.empty()) {
            const std::vector<uint8_t> response(toApp.begin(), toApp.end());
            toApp.clear();
            s.bytesIn(mc::ByteView{response.data(), response.size()}, nowMs());
            drain();
            continue;
        }

        // Nothing is on the wire: this is the application's own wait until the engine needs
        // time (the next round, or a response deadline), then tick().
        const mc::TimeMs deadline = s.nextDeadline();
        const mc::TimeMs now = nowMs();
        if (deadline == mc::kNoDeadline) {
            break;
        }
        if (deadline > now) {
            std::this_thread::sleep_for(std::chrono::milliseconds(deadline - now));
        }
        s.tick(nowMs());
        drain();
    }

    if (faulted) {
        std::printf("link fault\n");
        return 1;
    }
    if (roundsDone < kRounds) { // e.g. the loop ended because the Session has no deadline
        std::printf("stopped after %u of %u rounds\n", static_cast<unsigned>(roundsDone),
                    static_cast<unsigned>(kRounds));
        return 1;
    }
    std::printf("done after %u rounds\n", static_cast<unsigned>(roundsDone));
    return 0;
}
