// tests/bench/bench_session.cpp (spec SPEC-core-session.md, Project Structure:
// "tests/bench/bench_session.cpp: encode-free round cost vs subscribed points, regression
// tracking only"; T-027): NOT a ctest test -- no add_test(), no pass/fail threshold. Prints one
// line per subscribed-point count, average wall-clock time per steady-state polling round (a
// scripted peer answers immediately, heartbeat on, values changing every round -- the same
// scenario ALC-01 checks for zero allocation, timed here instead) and per point, so a human (or a
// future automated regression check) can watch the trend over time. Built only with
// -DMC_BUILD_BENCH=ON, typically Release (docs/spec/SPEC-core-session.md's own quoted command:
// `cmake -S . -B build/cmake-bench -DMC_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release`).
//
// Reuses tests/core/session/harness.h (FakeClock/ScriptedPeer/PeerScript) directly: it has no
// doctest dependency of its own (only the test_*.cpp files that use it do), so it links cleanly
// into this plain executable too (tests/bench/CMakeLists.txt compiles harness.cpp alongside this
// file).
#include "harness.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/session.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using mc::ByteView;
using mc::Device;
using mc::DeviceType;
using mc::FrameConfig;
using mc::Output;
using mc::OutputKind;
using mc::Request;
using mc::Session;
using mc::SessionConfig;
using mc::TimeMs;
using mc::test::PeerScript;
using mc::test::ScriptedPeer;

namespace {

struct BenchResult {
    uint32_t points{0};
    double nsPerRound{0.0};
    double nsPerPoint{0.0};
};

// One disjoint D-device subscription per chunk (gap 32 points >> autoGap(16): never merged), so
// `deviceCount` is also this session's own chunk count. Returns a default-constructed
// (points == 0) result on any setup failure, printed by main() as an error line.
BenchResult benchOne(const FrameConfig& frame, uint32_t pointsPerDevice, uint32_t deviceCount,
                      int rounds) {
    SessionConfig cfg;
    cfg.heartbeat.enabled = true;
    cfg.heartbeat.device = Device{DeviceType::M, 2000};

    auto created = Session::create(frame, cfg);
    if (!created.hasValue()) {
        return BenchResult{};
    }
    Session s = std::move(created.value());

    constexpr uint32_t kGap = 32;
    for (uint32_t i = 0; i < deviceCount; ++i) {
        uint32_t head = i * (pointsPerDevice + kGap);
        if (!s.subscribe(Device{DeviceType::D, head}, pointsPerDevice).hasValue()) {
            return BenchResult{};
        }
    }

    ScriptedPeer peer(frame);
    TimeMs now = 0;
    Output out;

    // A write ack's own bytes never depend on the request (harness.cpp's buildReadData(): "a
    // write's response carries no data"), so one fixed heartbeat-ack buffer, built once here,
    // covers every round's heartbeat write regardless of which bit Session itself sent.
    uint8_t one = 1;
    auto hbAck =
        peer.respond(Request::writeBits(cfg.heartbeat.device, ByteView{&one, 1}), PeerScript{});

    // Drives exactly one round: the heartbeat write, then every device's own poll chunk (in
    // subscription order, matching how dispatch() sends them), each answered with every word set
    // to `value` -- changing every round, like ALC-01's own test, so ValueStore::apply() takes
    // its full diff-and-publish path rather than a quiescent no-op one.
    auto driveRound = [&](uint16_t value) {
        if (!s.nextOutput(out)) {
            return;
        }
        now += 1;
        s.bytesIn(ByteView{hbAck.data(), hbAck.size()}, now);

        PeerScript p;
        p.kind = PeerScript::Kind::Ok;
        p.words.assign(pointsPerDevice, value);
        for (uint32_t i = 0; i < deviceCount; ++i) {
            if (!s.nextOutput(out)) {
                return;
            }
            uint32_t head = i * (pointsPerDevice + kGap);
            auto bytes = peer.respond(
                Request::readWords(Device{DeviceType::D, head},
                                    static_cast<uint16_t>(pointsPerDevice)),
                p);
            now += 1;
            s.bytesIn(ByteView{bytes.data(), bytes.size()}, now);
        }

        while (s.nextOutput(out)) {
            // Snapshot(s) + CycleDone; not inspected here (see ALC-01's own test for the
            // functional check of this exact sequence).
        }
    };

    s.linkUp(now);
    driveRound(0); // Round 1: warm-up (re-plan settles here; not timed).

    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < rounds; ++r) {
        now += 1000; // Past cycleIntervalMs's default (100): starts this round via tick().
        s.tick(now);
        driveRound(static_cast<uint16_t>(r + 1));
    }
    auto t1 = std::chrono::steady_clock::now();

    double totalNs = std::chrono::duration<double, std::nano>(t1 - t0).count();
    uint32_t points = pointsPerDevice * deviceCount;
    BenchResult result{};
    result.points = points;
    result.nsPerRound = totalNs / rounds;
    result.nsPerPoint = points > 0 ? result.nsPerRound / points : 0.0;
    return result;
}

} // namespace

int main() {
    FrameConfig frame = FrameConfig::frame3E();
    const int rounds = 200;

    struct Config {
        uint32_t pointsPerDevice;
        uint32_t deviceCount;
    };
    // A spread of subscribed-point counts and chunk counts, so the printed trend shows both "more
    // points, same chunk count" and "more chunks, same point count" scaling (spec complexity
    // table: "one polling round, steady state: O(P + wire bytes of the round)").
    const Config configs[] = {
        {100, 1}, {100, 5}, {100, 10}, {100, 20}, {500, 20},
    };

    std::printf("mc_bench_session: encode-free round cost vs subscribed points "
                "(regression tracking only, no threshold)\n");
    std::printf("%12s %14s %14s\n", "points", "ns/round", "ns/point");
    for (const Config& c : configs) {
        BenchResult r = benchOne(frame, c.pointsPerDevice, c.deviceCount, rounds);
        if (r.points == 0) {
            std::printf("%12s %14s %14s  (setup failed: %u points/device x %u devices)\n", "-",
                         "-", "-", c.pointsPerDevice, c.deviceCount);
            continue;
        }
        std::printf("%12u %14.1f %14.2f\n", r.points, r.nsPerRound, r.nsPerPoint);
    }
    return 0;
}
