// INT-xx of SPEC-mock-plc.md "Integration: Session <-> MockPlc": the engine against the mock, in
// one process, over seeded fragmenting pipes and a fake clock. Every scenario runs once per cell of
// combos() (the 12 cells of v1: 3E and 1E, each Binary and ASCII; 3C and 1C, formats 1 to 4); a
// doctest subcase per cell names the combination when something fails.
//
// Skipped on purpose: INT-04, INT-05 and INT-06 (random access, tagged v1.1) and INT-09 (4C
// format 5, tagged v2) -- the spec keeps them out of v1, so there is no test body for them.
// INT-11 and INT-12 have an Ethernet body (fault at once, reopen) and a serial body (EOT, flush,
// retries, reopen=false); the clock is the rig's own number, so the flush windows are jumps.
//
// Numbers that depend on the frame (INT-07 request counts, the PLC error codes of INT-08 and
// INT-16) are literals of the reference spec or of MockOptions, chosen per family below, never read
// back from the code under test.
#include "doctest/doctest.h"

#include "rig.h"

#include "mc/core/device.h"
#include "mc/core/limits.h"
#include "mc/core/poll_plan.h"
#include "mc/core/value_store.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace mc;
using namespace mc::test;

namespace {

// Runs `scenario(combo)` in its own subcase for every cell of the matrix.
#define INT_MATRIX(scenario)                                                                       \
    for (const Combo& combo_ : combos()) {                                                         \
        SUBCASE(combo_.name.c_str()) { scenario(combo_); }                                         \
    }

Device dn(DeviceType type, uint32_t number) { return Device{type, number}; }

bool is1e(const Combo& combo) { return combo.frame.frame == FrameType::F1E; }
bool is1c(const Combo& combo) { return combo.frame.frame == FrameType::F1C; }
bool isSerial(const Combo& combo) { return combo.frame.isSerial(); }

// ---- INT-01 -----------------------------------------------------------------------------------

void int01(const Combo& combo) {
    Rig rig(combo);
    rig.linkUp();

    const ByteBuf data = wordsLe({0x1995, 0x1202, 0x1130});
    Expected<RequestId> wrote = rig.submit(Request::writeWords(dev("D100"), view(data)));
    REQUIRE(wrote.hasValue());
    const std::vector<Event> writeDone = rig.requestDones(wrote.value());
    REQUIRE(writeDone.size() == 1);
    CHECK(writeDone[0].error.ok());
    CHECK(writeDone[0].payload.empty());
    CHECK(rig.mock().word(dev("D100")) == 0x1995);
    CHECK(rig.mock().word(dev("D101")) == 0x1202);
    CHECK(rig.mock().word(dev("D102")) == 0x1130);

    Expected<RequestId> read = rig.submit(Request::readWords(dev("D100"), 3));
    REQUIRE(read.hasValue());
    CHECK(read.value() != wrote.value());
    const std::vector<Event> readDone = rig.requestDones(read.value());
    REQUIRE(readDone.size() == 1);
    CHECK(readDone[0].error.ok());
    CHECK(readDone[0].payload == data);

    CHECK(rig.ofKind(OutputKind::RequestDone).size() == 2);
    CHECK(rig.mock().requests().size() == 2);
}

// ---- INT-02 -----------------------------------------------------------------------------------

void int02(const Combo& combo) {
    Rig rig(combo);
    rig.linkUp();

    const ByteBuf bits = {1, 1, 0, 0, 1, 1, 0, 0};
    Expected<RequestId> wrote = rig.submit(Request::writeBits(dev("M100"), view(bits)));
    REQUIRE(wrote.hasValue());
    const std::vector<Event> writeDone = rig.requestDones(wrote.value());
    REQUIRE(writeDone.size() == 1);
    CHECK(writeDone[0].error.ok());
    for (uint32_t i = 0; i < bits.size(); ++i) {
        CHECK(rig.mock().bit(dn(DeviceType::M, 100 + i)) == (bits[i] != 0));
    }

    Expected<RequestId> read = rig.submit(Request::readBits(dev("M100"), 8));
    REQUIRE(read.hasValue());
    const std::vector<Event> readDone = rig.requestDones(read.value());
    REQUIRE(readDone.size() == 1);
    CHECK(readDone[0].error.ok());
    CHECK(readDone[0].payload == bits);
}

// ---- INT-03 -----------------------------------------------------------------------------------

void int03(const Combo& combo) {
    Rig rig(combo);
    rig.linkUp();
    // The point after the five written must survive the padding of an odd count.
    rig.mock().setBit(dev("M105"), true);

    const ByteBuf bits = {1, 0, 1, 1, 0};
    Expected<RequestId> wrote = rig.submit(Request::writeBits(dev("M100"), view(bits)));
    REQUIRE(wrote.hasValue());
    const std::vector<Event> writeDone = rig.requestDones(wrote.value());
    REQUIRE(writeDone.size() == 1);
    CHECK(writeDone[0].error.ok());
    for (uint32_t i = 0; i < bits.size(); ++i) {
        CHECK(rig.mock().bit(dn(DeviceType::M, 100 + i)) == (bits[i] != 0));
    }
    CHECK(rig.mock().bit(dev("M105")) == true);

    Expected<RequestId> read = rig.submit(Request::readBits(dev("M100"), 5));
    REQUIRE(read.hasValue());
    const std::vector<Event> readDone = rig.requestDones(read.value());
    REQUIRE(readDone.size() == 1);
    CHECK(readDone[0].error.ok());
    CHECK(readDone[0].payload == bits);
}

// ---- INT-07 -----------------------------------------------------------------------------------

uint16_t pattern(uint32_t k) { return static_cast<uint16_t>(k * 7u + 1u); }

void int07(const Combo& combo) {
    Rig rig(combo);
    ByteBuf expectedPayload;
    for (uint32_t k = 0; k < 2000; ++k) {
        rig.mock().setWord(dn(DeviceType::D, k), pattern(k));
        expectedPayload.push_back(static_cast<uint8_t>(pattern(k) & 0xFFu));
        expectedPayload.push_back(static_cast<uint8_t>(pattern(k) >> 8));
    }
    rig.linkUp();
    rig.mock().clearLog();

    const Request read = Request::readWords(dev("D0"), 2000);
    const Expected<size_t> chunks = chunkCount(read, rig.frame());
    REQUIRE(chunks.hasValue());
    // Spec §4.4, pinned here by the reference spec and not derived from chunkCount(). 3E (0401
    // word, word device, iQ-R/Q/L): 960 points for Binary and ASCII alike, so 2000 words are
    // 960 + 960 + 80. 1E (01H, word device): 256 points for both codes, so 2000 words are
    // 7 x 256 + 208 = 8 requests. 3C (QnA family, same 0401 limit as 3E): 960 + 960 + 80 in every
    // format. 1C (WR, word device): 64 points, so 2000 words are 31 x 64 + 16 = 32 requests.
    std::vector<uint32_t> specHeads;
    std::vector<uint16_t> specCounts;
    if (is1c(combo)) {
        for (uint32_t i = 0; i < 31; ++i) {
            specHeads.push_back(i * 64);
            specCounts.push_back(64);
        }
        specHeads.push_back(1984);
        specCounts.push_back(16);
    } else if (is1e(combo)) {
        for (uint32_t i = 0; i < 7; ++i) {
            specHeads.push_back(i * 256);
            specCounts.push_back(256);
        }
        specHeads.push_back(1792);
        specCounts.push_back(208);
    } else {
        specHeads = {0, 960, 1920};
        specCounts = {960, 960, 80};
    }
    CHECK(chunks.value() == specHeads.size());
    std::vector<Chunk> expected(chunks.value());
    REQUIRE(chunk(read, rig.frame(), expected.data(), expected.size()).hasValue());

    Expected<RequestId> id = rig.submit(read);
    REQUIRE(id.hasValue());
    const std::vector<Event> done = rig.requestDones(id.value());
    REQUIRE(done.size() == 1);
    CHECK(done[0].error.ok());
    CHECK(done[0].payload == expectedPayload);

    const std::vector<MockRequestRecord>& log = rig.mock().requests();
    REQUIRE(log.size() == expected.size());
    REQUIRE(log.size() == specHeads.size());
    for (size_t i = 0; i < log.size(); ++i) {
        CHECK(log[i].head.number == specHeads[i]);
        CHECK(log[i].count == specCounts[i]);
    }
    for (size_t i = 0; i < log.size(); ++i) {
        CHECK(log[i].op == Op::ReadWords);
        CHECK(log[i].head == dn(DeviceType::D, expected[i].headNumber));
        CHECK(log[i].count == expected[i].count);
        CHECK(log[i].answered);
    }
}

// ---- INT-08 -----------------------------------------------------------------------------------

void int08(const Combo& combo) {
    // 3E and 3C: C051H (V-3E-B-10, V-3C1-NAKWRITE). 1E: 50H (V-1E-B-12), a u8 end code with no
    // abnormal code. 1C: a u8 NAK code, 06H (V-1C1-08).
    const uint16_t kCode = is1e(combo) ? 0x50 : (is1c(combo) ? 0x06 : 0xC051);
    Rig rig(combo);
    rig.linkUp();
    rig.mock().failRange(DeviceType::D, 100, 100, kCode);

    Expected<RequestId> id = rig.submit(Request::readWords(dev("D100"), 1));
    REQUIRE(id.hasValue());
    const std::vector<Event> done = rig.requestDones(id.value());
    REQUIRE(done.size() == 1);
    CHECK(done[0].error.category == ErrorCategory::Plc);
    CHECK(done[0].error.code == ErrorCode::PlcError);
    CHECK(done[0].error.plcCode == kCode);
    CHECK(done[0].payload.empty());
    REQUIRE(rig.mock().requests().size() == 1);
    CHECK(rig.mock().requests()[0].answered);
    CHECK(rig.mock().requests()[0].answeredWith.plcCode == kCode);
    CHECK(done[0].error.abnormalCode == 0);

    // A PLC error is not a link fault: the link stays usable, outside the range too.
    CHECK_FALSE(rig.session().isFaulted());
    Expected<RequestId> next = rig.submit(Request::readWords(dev("D101"), 1));
    REQUIRE(next.hasValue());
    const std::vector<Event> nextDone = rig.requestDones(next.value());
    REQUIRE(nextDone.size() == 1);
    CHECK(nextDone[0].error.ok());

    // 1E only: end code 5BH is followed by an abnormal code (V-1E-B-11), and both reach the caller.
    if (is1e(combo)) {
        rig.mock().failRange(DeviceType::D, 200, 200, 0x5B, 0x10);
        Expected<RequestId> abnormal = rig.submit(Request::readWords(dev("D200"), 1));
        REQUIRE(abnormal.hasValue());
        const std::vector<Event> abnormalDone = rig.requestDones(abnormal.value());
        REQUIRE(abnormalDone.size() == 1);
        CHECK(abnormalDone[0].error.category == ErrorCategory::Plc);
        CHECK(abnormalDone[0].error.plcCode == 0x5B);
        CHECK(abnormalDone[0].error.abnormalCode == 0x10);
        CHECK_FALSE(rig.session().isFaulted());
    }
}

// ---- INT-10 and INT-15 ------------------------------------------------------------------------

void seedInt10(MockPlc& plc) {
    for (uint32_t i = 0; i < 64; ++i) {
        plc.setWord(dn(DeviceType::D, 100 + i), static_cast<uint16_t>(1000 + i));
        plc.setBit(dn(DeviceType::M, i), i % 3 == 0);
    }
    for (uint32_t i = 0; i < 32; ++i) {
        plc.setBit(dn(DeviceType::X, i), i % 2 == 0);
    }
}

// Subscribe D100x64, M0x64, X0x32; round 1; change D105 and M5 in the mock; round 2.
void runInt10(Rig& rig) {
    REQUIRE(rig.session().subscribe(dev("D100"), 64).hasValue());
    REQUIRE(rig.session().subscribe(dev("M0"), 64).hasValue());
    REQUIRE(rig.session().subscribe(dev("X0"), 32).hasValue());
    seedInt10(rig.mock());
    rig.linkUp();
    rig.mock().setWord(dev("D105"), 9999);
    rig.mock().setBit(dev("M5"), true);
    REQUIRE(rig.runRound());
}

// One event of a round reduced to what the spec's timeline names.
using Step = std::pair<OutputKind, DeviceType>;

std::vector<Step> steps(const std::vector<Event>& round) {
    std::vector<Step> out;
    for (const Event& e : round) {
        out.emplace_back(e.kind, e.deviceType);
    }
    return out;
}

void int10(const Combo& combo) {
    Rig rig(combo);
    runInt10(rig);

    // Round 1: silent baseline, then one snapshot per type in DeviceType order (X, M, D).
    const std::vector<Event> round1 = roundEvents(rig.events(), 1);
    const std::vector<Step> expectedRound1 = {{OutputKind::Snapshot, DeviceType::X},
                                              {OutputKind::Snapshot, DeviceType::M},
                                              {OutputKind::Snapshot, DeviceType::D},
                                              {OutputKind::CycleDone, DeviceType::D}};
    CHECK(steps(round1) == expectedRound1);
    CHECK(allChanges(round1).empty());
    for (const Event& e : round1) {
        if (e.kind == OutputKind::Snapshot) {
            REQUIRE_FALSE(e.chunks.empty());
            for (const ChunkInfo& c : e.chunks) {
                CHECK(c.state == ChunkState::Ok);
            }
        } else {
            CHECK(e.cycle.round == 1);
            CHECK(e.cycle.failedChunks == 0);
        }
    }

    // Round 2: only D105 and M5 changed. A ValuesChanged precedes its own type's Snapshot, and
    // every type still gets one snapshot.
    const std::vector<Event> round2 = roundEvents(rig.events(), 2);
    const std::vector<Step> expectedRound2 = {{OutputKind::Snapshot, DeviceType::X},
                                              {OutputKind::ValuesChanged, DeviceType::M},
                                              {OutputKind::Snapshot, DeviceType::M},
                                              {OutputKind::ValuesChanged, DeviceType::D},
                                              {OutputKind::Snapshot, DeviceType::D},
                                              {OutputKind::CycleDone, DeviceType::D}};
    CHECK(steps(round2) == expectedRound2);
    const std::vector<Change> changes = allChanges(round2);
    REQUIRE(changes.size() == 2);
    const auto has = [&](Device d, uint16_t oldValue, uint16_t newValue) {
        return std::any_of(changes.begin(), changes.end(), [&](const Change& c) {
            return c.device == d && c.oldValue == oldValue && c.newValue == newValue;
        });
    };
    CHECK(has(dev("M5"), 0, 1));
    CHECK(has(dev("D105"), 1005, 9999));

    const ValueStore& values = rig.session().values();
    CHECK(values.state(dev("D105")) == PointState::Valid);
    CHECK(values.word(dev("D105")) == 9999);
    CHECK(values.bit(dev("M5")));
    CHECK(values.word(dev("D100")) == 1000);
}

// ---- INT-11 -----------------------------------------------------------------------------------

// The bytes the Session sends for an EOT: 04H, and 04H 0DH 0AH in format 4 (spec 2.7).
ByteBuf eotBytes(const Combo& combo) {
    return combo.frame.format == SerialFormat::Format4 ? ByteBuf{0x04, 0x0D, 0x0A} : ByteBuf{0x04};
}

// Serial: the mock never answers. Each of the 1 + readRetries (= 3) attempts times out after 3000
// ms (spec 8.3, serial), sends an EOT (which the mock counts although it is muted) and flushes for
// serialFlushMs (50 ms); the third consecutive link error (maxConsecutiveLinkErrors = 3) is a
// LinkFault with reopenTransport = false.
void int11Serial(const Combo& combo) {
    Rig rig(combo);
    REQUIRE(rig.session().subscribe(dev("D100"), 64).hasValue());
    rig.mock().setWord(dev("D100"), 7);
    rig.linkUp();
    REQUIRE(rig.cycleCount() == 1);
    rig.clearEvents();
    rig.mock().clearLog();
    const uint32_t eotBefore = rig.mock().eotCount();

    rig.mock().mute(true);
    CHECK_FALSE(rig.runRound()); // the round starts, its request is swallowed, no CycleDone
    REQUIRE(rig.mock().requests().size() == 1);
    CHECK_FALSE(rig.mock().requests()[0].answered);
    CHECK(rig.mock().eotCount() == eotBefore);
    CHECK(rig.session().nextDeadline() == rig.now() + 3000);

    for (uint32_t attempt = 1; attempt <= 3; ++attempt) {
        CAPTURE(attempt);
        REQUIRE(rig.jumpToDeadline()); // no first byte within the timeout
        CHECK(rig.session().stats().timeouts == attempt);
        CHECK(rig.mock().eotCount() == eotBefore + attempt);
        if (attempt < 3) {
            CHECK_FALSE(rig.session().isFaulted());
            CHECK(rig.ofKind(OutputKind::LinkFault).empty());
            CHECK(rig.session().nextDeadline() == rig.now() + 50); // serialFlushMs, no bytes
            REQUIRE(rig.jumpToDeadline());                         // silent: the read is resent
            CHECK(rig.session().stats().retries == attempt);
            CHECK(rig.mock().requests().size() == attempt + 1);
            CHECK(rig.session().nextDeadline() == rig.now() + 3000);
        }
    }

    // The request log shows the same read three times: the original and readRetries = 2 resends.
    const std::vector<MockRequestRecord>& log = rig.mock().requests();
    REQUIRE(log.size() == 3);
    for (const MockRequestRecord& r : log) {
        CHECK(r.op == Op::ReadWords);
        CHECK(r.head == dev("D100"));
        CHECK(r.count == 64);
        CHECK_FALSE(r.answered);
    }

    const std::vector<Event> faults = rig.ofKind(OutputKind::LinkFault);
    REQUIRE(faults.size() == 1);
    CHECK(faults[0].fault == LinkFaultKind::Timeout);
    CHECK(faults[0].error.code == ErrorCode::Timeout);
    CHECK_FALSE(faults[0].reopenTransport);
    CHECK(rig.session().isFaulted());
    CHECK(rig.session().nextDeadline() == kNoDeadline);
    CHECK(rig.session().plan().chunk(0).state == ChunkState::Failed);
    CHECK(rig.session().stats().retries == 2);
    CHECK(rig.session().stats().eotsSent == 3);

    // On the wire: request, EOT, request, EOT, request, EOT; nothing after the fault.
    const ByteBuf eot = eotBytes(combo);
    std::vector<Event> sends = rig.ofKind(OutputKind::Send);
    REQUIRE(sends.size() == 6);
    for (size_t i = 0; i < sends.size(); ++i) {
        CAPTURE(i);
        if (i % 2 == 1) {
            CHECK(sends[i].bytes == eot);
        } else {
            CHECK(sends[i].bytes == sends[0].bytes);
            CHECK(sends[i].bytes != eot);
        }
    }
    rig.advance(60000);
    rig.tick();
    CHECK(rig.ofKind(OutputKind::Send).size() == 6);
    CHECK(rig.mock().requests().size() == 3);

    // linkDown + linkUp restart round 1.
    rig.mock().mute(false);
    rig.linkDown();
    rig.clearEvents();
    rig.linkUp();
    const std::vector<Event> round1 = roundEvents(rig.events(), 1);
    const std::vector<Step> expectedRound1 = {{OutputKind::Snapshot, DeviceType::D},
                                              {OutputKind::CycleDone, DeviceType::D}};
    CHECK(steps(round1) == expectedRound1);
    CHECK(rig.session().values().state(dev("D100")) == PointState::Valid);
    CHECK(rig.session().values().word(dev("D100")) == 7);
    CHECK_FALSE(rig.session().isFaulted());
}

void int11(const Combo& combo) {
    if (isSerial(combo)) {
        int11Serial(combo);
        return;
    }
    Rig rig(combo);
    REQUIRE(rig.session().subscribe(dev("D100"), 64).hasValue());
    rig.mock().setWord(dev("D100"), 7);
    rig.linkUp();
    REQUIRE(rig.cycleCount() == 1);
    rig.clearEvents();
    rig.mock().clearLog();

    rig.mock().mute(true);
    CHECK_FALSE(rig.runRound()); // the round starts, its request is swallowed, no CycleDone
    REQUIRE(rig.mock().requests().size() == 1);
    CHECK_FALSE(rig.mock().requests()[0].answered);
    CHECK(rig.ofKind(OutputKind::LinkFault).empty());
    CHECK(rig.session().nextDeadline() == rig.now() + rig.frame().effectiveTimeoutMs());

    // No sleeping: the deadline passes by moving the clock and ticking.
    REQUIRE(rig.jumpToDeadline());
    const std::vector<Event> faults = rig.ofKind(OutputKind::LinkFault);
    REQUIRE(faults.size() == 1);
    CHECK(faults[0].fault == LinkFaultKind::Timeout);
    CHECK(faults[0].error.code == ErrorCode::Timeout);
    CHECK(faults[0].reopenTransport);
    CHECK(rig.session().isFaulted());
    CHECK(rig.session().nextDeadline() == kNoDeadline);
    CHECK(rig.session().plan().chunk(0).state == ChunkState::Failed);
    CHECK(rig.session().stats().timeouts == 1);
    CHECK(rig.session().stats().retries == 0);

    // Ethernet never resends: the muted request stays the only one, however long we wait.
    rig.advance(60000);
    rig.tick();
    CHECK(rig.ofKind(OutputKind::Send).size() == 1);
    CHECK(rig.mock().requests().size() == 1);

    // linkDown + linkUp restart round 1.
    rig.mock().mute(false);
    rig.linkDown();
    rig.clearEvents();
    rig.linkUp();
    const std::vector<Event> round1 = roundEvents(rig.events(), 1);
    const std::vector<Step> expectedRound1 = {{OutputKind::Snapshot, DeviceType::D},
                                              {OutputKind::CycleDone, DeviceType::D}};
    CHECK(steps(round1) == expectedRound1);
    CHECK(rig.session().values().state(dev("D100")) == PointState::Valid);
    CHECK(rig.session().values().word(dev("D100")) == 7);
    CHECK_FALSE(rig.session().isFaulted());
}

// ---- INT-12 -----------------------------------------------------------------------------------

// Serial: the mock sends one response with its SUM + 1. The Session discards the exchange, sends
// an EOT, flushes for serialFlushMs and resends the read, which now succeeds: no fault, and the
// values and changes of the round are the right ones.
void int12Serial(const Combo& combo) {
    Rig rig(combo);
    REQUIRE(rig.session().subscribe(dev("D100"), 64).hasValue());
    rig.mock().setWord(dev("D100"), 7);
    rig.linkUp();
    REQUIRE(rig.cycleCount() == 1);
    rig.clearEvents();
    rig.mock().clearLog();
    const uint32_t eotBefore = rig.mock().eotCount();
    rig.mock().setWord(dev("D101"), 9);

    rig.mock().corruptNext(Corruption::WrongSumCheck);
    CHECK_FALSE(rig.runRound()); // round 2: the damaged response ends the exchange
    CHECK(rig.mock().eotCount() == eotBefore + 1);
    CHECK(rig.ofKind(OutputKind::LinkFault).empty());
    CHECK_FALSE(rig.session().isFaulted());
    CHECK(rig.session().stats().protocolErrors == 1);
    CHECK(rig.session().stats().timeouts == 0);
    CHECK(rig.session().nextDeadline() == rig.now() + 50); // serialFlushMs of silence
    REQUIRE(rig.mock().requests().size() == 1);
    CHECK(rig.mock().requests()[0].answered); // the mock answered; the damage is on the wire

    REQUIRE(rig.jumpToDeadline()); // silent: the read is resent, the answer is intact
    CHECK(rig.cycleCount() == 1);
    CHECK(rig.ofKind(OutputKind::LinkFault).empty());
    CHECK_FALSE(rig.session().isFaulted());
    CHECK(rig.session().stats().retries == 1);
    CHECK(rig.mock().eotCount() == eotBefore + 1);
    const std::vector<MockRequestRecord>& log = rig.mock().requests();
    REQUIRE(log.size() == 2);
    for (const MockRequestRecord& r : log) {
        CHECK(r.op == Op::ReadWords);
        CHECK(r.head == dev("D100"));
        CHECK(r.count == 64);
        CHECK(r.answered);
    }

    const std::vector<Event> round2 = roundEvents(rig.events(), 2);
    const std::vector<Step> expectedRound2 = {{OutputKind::ValuesChanged, DeviceType::D},
                                              {OutputKind::Snapshot, DeviceType::D},
                                              {OutputKind::CycleDone, DeviceType::D}};
    CHECK(steps(round2) == expectedRound2);
    const std::vector<Change> changes = allChanges(round2);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].device == dev("D101"));
    CHECK(changes[0].oldValue == 0);
    CHECK(changes[0].newValue == 9);
    REQUIRE(round2.size() == 3);
    CHECK(round2[2].cycle.requests == 2); // the resend is a request of the round
    CHECK(round2[2].cycle.failedChunks == 0);
    CHECK(rig.session().values().state(dev("D100")) == PointState::Valid);
    CHECK(rig.session().values().word(dev("D100")) == 7);
    CHECK(rig.session().values().word(dev("D101")) == 9);
}

// Serial: junk before the start byte is skipped (spec 6.3), so a response with a JunkPrefix is
// accepted as it is: no EOT, no resend, no link error, the values arrive. Also for the largest
// response, which the old buffer bound would have called an overflow.
void int12JunkSerial(const Combo& combo) {
    Rig rig(combo);
    REQUIRE(rig.session().subscribe(dev("D100"), 64).hasValue());
    rig.mock().setWord(dev("D100"), 7);
    rig.linkUp();
    REQUIRE(rig.cycleCount() == 1);
    rig.clearEvents();
    rig.mock().clearLog();
    const uint32_t eotBefore = rig.mock().eotCount();
    rig.mock().setWord(dev("D101"), 9);

    rig.mock().corruptNext(Corruption::JunkPrefix);
    REQUIRE(rig.runRound());
    CHECK(rig.mock().eotCount() == eotBefore);
    CHECK(rig.mock().requests().size() == 1);
    CHECK(rig.ofKind(OutputKind::LinkFault).empty());
    CHECK_FALSE(rig.session().isFaulted());
    CHECK(rig.session().stats().protocolErrors == 0);
    CHECK(rig.session().stats().timeouts == 0);
    CHECK(rig.session().stats().retries == 0);
    CHECK(rig.session().stats().eotsSent == 0);

    const std::vector<Event> round2 = roundEvents(rig.events(), 2);
    REQUIRE(round2.size() == 3);
    CHECK(round2[2].cycle.requests == 1);
    CHECK(round2[2].cycle.failedChunks == 0);
    const std::vector<Change> changes = allChanges(round2);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].device == dev("D101"));
    CHECK(changes[0].newValue == 9);
    CHECK(rig.session().values().word(dev("D100")) == 7);
}

void int12(const Combo& combo) {
    if (isSerial(combo)) {
        int12Serial(combo);
        int12JunkSerial(combo);
        return;
    }
    Rig rig(combo);
    REQUIRE(rig.session().subscribe(dev("D100"), 64).hasValue());
    rig.mock().setWord(dev("D100"), 7);
    rig.linkUp();
    REQUIRE(rig.cycleCount() == 1);
    rig.clearEvents();

    rig.mock().corruptNext(Corruption::WrongSubheader);
    CHECK_FALSE(rig.runRound());
    const std::vector<Event> faults = rig.ofKind(OutputKind::LinkFault);
    REQUIRE(faults.size() == 1);
    CHECK(faults[0].fault == LinkFaultKind::ProtocolError);
    CHECK(faults[0].reopenTransport);
    CHECK(rig.session().isFaulted());
    CHECK(rig.session().nextDeadline() == kNoDeadline);

    // The one damaged response is used up; after linkDown + linkUp the link is clean again.
    rig.linkDown();
    rig.clearEvents();
    rig.linkUp();
    CHECK(rig.ofKind(OutputKind::LinkFault).empty());
    CHECK(rig.cycleCount() == 1);
    CHECK(rig.session().values().state(dev("D100")) == PointState::Valid);
    CHECK(rig.session().values().word(dev("D100")) == 7);
}

// ---- INT-13 -----------------------------------------------------------------------------------

using ChangeKey = std::tuple<int, uint32_t, unsigned, unsigned>;
using PointKey = std::tuple<int, uint32_t, int, unsigned>;

struct Trace {
    Op mReadOp{Op::ReadWords};
    std::vector<std::vector<ChangeKey>> changesPerRound;
    std::vector<PointKey> finalPoints;
};

Trace evolve(const Combo& combo, bool bitsAsWords) {
    SessionConfig cfg;
    cfg.plan.bitsAsWords = bitsAsWords;
    Rig rig(combo, cfg);
    for (uint32_t i = 0; i < 64; ++i) {
        rig.mock().setBit(dn(DeviceType::M, i), i % 5 == 0);
    }
    for (uint32_t i = 0; i < 32; ++i) {
        rig.mock().setBit(dn(DeviceType::X, i), i % 4 == 1);
    }
    for (uint32_t i = 0; i < 8; ++i) {
        rig.mock().setWord(dn(DeviceType::D, 100 + i), static_cast<uint16_t>(i * 3));
    }
    REQUIRE(rig.session().subscribe(dev("M0"), 64).hasValue());
    REQUIRE(rig.session().subscribe(dev("X0"), 32).hasValue());
    REQUIRE(rig.session().subscribe(dev("D100"), 8).hasValue());
    rig.linkUp();

    // The same memory evolution for both settings; round 3 changes nothing.
    MockPlc& plc = rig.mock();
    const std::vector<void (*)(MockPlc&)> mods = {
        [](MockPlc& p) {
            p.setBit(dn(DeviceType::M, 0), true);
            p.setBit(dn(DeviceType::M, 15), true);
            p.setBit(dn(DeviceType::M, 16), true);
            p.setWord(dn(DeviceType::D, 103), 5);
        },
        [](MockPlc&) {},
        [](MockPlc& p) {
            p.setBit(dn(DeviceType::M, 15), false);
            p.setBit(dn(DeviceType::M, 63), true);
            p.setBit(dn(DeviceType::X, 31), true);
            p.setWord(dn(DeviceType::D, 100), 0xFFFF);
        },
        [](MockPlc& p) {
            p.setWord(dn(DeviceType::M, 32), 0xA5A5); // 16 points of M as one word
            p.setWord(dn(DeviceType::D, 103), 0);
        },
    };
    for (auto mod : mods) {
        mod(plc);
        REQUIRE(rig.runRound());
    }

    Trace trace;
    const auto [first, last] = rig.session().plan().chunksOf(DeviceType::M);
    REQUIRE(last > first);
    trace.mReadOp = rig.session().plan().chunk(first).request.op;
    for (uint32_t round = 1; round <= 5; ++round) {
        std::vector<ChangeKey> keys;
        for (const Change& c : allChanges(roundEvents(rig.events(), round))) {
            keys.emplace_back(static_cast<int>(c.device.type), c.device.number, c.oldValue,
                              c.newValue);
        }
        trace.changesPerRound.push_back(std::move(keys));
    }
    const ValueStore& values = rig.session().values();
    for (uint32_t i = 0; i < 64; ++i) {
        const Device d = dn(DeviceType::M, i);
        trace.finalPoints.emplace_back(static_cast<int>(d.type), i,
                                       static_cast<int>(values.state(d)), values.bit(d) ? 1u : 0u);
    }
    for (uint32_t i = 0; i < 32; ++i) {
        const Device d = dn(DeviceType::X, i);
        trace.finalPoints.emplace_back(static_cast<int>(d.type), i,
                                       static_cast<int>(values.state(d)), values.bit(d) ? 1u : 0u);
    }
    for (uint32_t i = 0; i < 8; ++i) {
        const Device d = dn(DeviceType::D, 100 + i);
        trace.finalPoints.emplace_back(static_cast<int>(d.type), 100 + i,
                                       static_cast<int>(values.state(d)), values.word(d));
    }
    return trace;
}

void int13(const Combo& combo) {
    const Trace words = evolve(combo, true);
    const Trace bits = evolve(combo, false);

    // The switch really changed what goes on the wire ...
    CHECK(words.mReadOp == Op::ReadWords);
    CHECK(bits.mReadOp == Op::ReadBits);
    // ... and nothing the application sees.
    REQUIRE(words.changesPerRound.size() == bits.changesPerRound.size());
    for (size_t r = 0; r < words.changesPerRound.size(); ++r) {
        CAPTURE(r + 1);
        CHECK(words.changesPerRound[r] == bits.changesPerRound[r]);
    }
    CHECK(words.finalPoints == bits.finalPoints);
    // Guard against a vacuous comparison: round 1 is silent, later rounds did change something.
    CHECK(words.changesPerRound[0].empty());
    CHECK_FALSE(words.changesPerRound[1].empty());
    CHECK(words.changesPerRound[2].empty());
    CHECK_FALSE(words.changesPerRound[3].empty());
    CHECK_FALSE(words.changesPerRound[4].empty());
}

// ---- INT-14 -----------------------------------------------------------------------------------

void int14(const Combo& combo) {
    SessionConfig cfg;
    cfg.heartbeat.enabled = true; // M2000, the default device
    Rig rig(combo, cfg);
    REQUIRE(rig.session().subscribe(dev("D0"), 4).hasValue());
    rig.linkUp();

    const bool expected[5] = {true, false, true, false, true};
    for (size_t r = 0; r < 5; ++r) {
        if (r > 0) {
            REQUIRE(rig.runRound());
        }
        CAPTURE(r + 1);
        CHECK(rig.mock().bit(dev("M2000")) == expected[r]);
    }

    const std::vector<Event> cycles = rig.ofKind(OutputKind::CycleDone);
    REQUIRE(cycles.size() == 5);
    for (size_t r = 0; r < cycles.size(); ++r) {
        CHECK(cycles[r].cycle.round == r + 1);
        CHECK(cycles[r].cycle.heartbeatOk);
    }
    CHECK(rig.ofKind(OutputKind::RequestDone).empty());

    // The heartbeat write is the first frame of every round: write, read, write, read, ...
    const std::vector<MockRequestRecord>& log = rig.mock().requests();
    REQUIRE(log.size() == 10);
    for (size_t i = 0; i < log.size(); ++i) {
        CAPTURE(i);
        if (i % 2 == 0) {
            CHECK(log[i].op == Op::WriteBits);
            CHECK(log[i].head == dev("M2000"));
        } else {
            CHECK(log[i].op == Op::ReadWords);
        }
    }
}

// ---- INT-15 -----------------------------------------------------------------------------------

void appendHex(std::ostringstream& os, const ByteBuf& bytes) {
    char buf[3];
    for (uint8_t b : bytes) {
        std::snprintf(buf, sizeof(buf), "%02X", b);
        os << buf;
    }
}

void appendError(std::ostringstream& os, const Error& e) {
    os << static_cast<int>(e.category) << '/' << static_cast<int>(e.code) << '/' << e.plcCode;
}

// Every output of a run, in order, as text: two runs are the same run exactly when it is equal.
std::string canonical(const std::vector<Event>& events) {
    std::ostringstream os;
    for (const Event& e : events) {
        os << static_cast<int>(e.kind) << ' ';
        switch (e.kind) {
        case OutputKind::Send:
            appendHex(os, e.bytes);
            break;
        case OutputKind::ValuesChanged:
            os << static_cast<int>(e.deviceType) << ' ' << e.round;
            for (const Change& c : e.changes) {
                os << " [" << static_cast<int>(c.device.type) << ':' << c.device.number << ' '
                   << c.oldValue << '>' << c.newValue << ']';
            }
            break;
        case OutputKind::Snapshot:
            os << static_cast<int>(e.deviceType) << ' ' << e.round;
            for (const ChunkInfo& c : e.chunks) {
                os << " [" << static_cast<int>(c.request.head.type) << ':' << c.request.head.number
                   << 'x' << c.request.count << ' ' << static_cast<int>(c.state) << ' ';
                appendError(os, c.lastError);
                os << ' ' << c.lastOkRound << ']';
            }
            break;
        case OutputKind::CycleDone:
            os << e.cycle.round << ' ' << e.cycle.startedAt << ' ' << e.cycle.durationMs << ' '
               << e.cycle.requests << ' ' << e.cycle.failedChunks << ' ' << e.cycle.heartbeatOk;
            break;
        case OutputKind::RequestDone:
            os << e.requestId << ' ';
            appendError(os, e.error);
            os << ' ';
            appendHex(os, e.payload);
            break;
        case OutputKind::LinkFault:
            os << static_cast<int>(e.fault) << ' ' << e.reopenTransport << ' ';
            appendError(os, e.error);
            break;
        }
        os << '\n';
    }
    return os.str();
}

void int15(const Combo& combo) {
    std::string reference;
    std::set<size_t> fragmentTotals;
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        Rig rig(combo, SessionConfig{}, seed);
        runInt10(rig);
        const std::string run = canonical(rig.events());
        if (seed == 1) {
            reference = run;
            REQUIRE_FALSE(reference.empty());
        }
        CAPTURE(seed);
        CHECK(run == reference);
        fragmentTotals.insert(rig.fragmentsWritten());
    }
    // The seeds really fragment differently, or the comparison above proves nothing.
    CHECK(fragmentTotals.size() > 1);
}

// ---- INT-16 -----------------------------------------------------------------------------------

void int16Case(const Combo& combo, bool bitsAsWords) {
    // 3E and 3C: C051H (V-3E-B-10). 1E: end code 5BH with abnormal code 10H (V-1E-B-11). 1C: the
    // u8 NAK code of MockOptions::outOfRange1c, 06H.
    const uint16_t kOutOfRange = is1e(combo) ? 0x5B : (is1c(combo) ? 0x06 : 0xC051);
    SessionConfig cfg;
    cfg.plan.bitsAsWords = bitsAsWords;
    Rig rig(combo, cfg);
    rig.mock().setDeviceLimit(DeviceType::M, 8190);
    for (uint32_t i = 0; i < 10; ++i) {
        rig.mock().setBit(dn(DeviceType::M, 8180 + i), i % 2 == 1);
    }
    REQUIRE(rig.session().subscribe(dev("M8180"), 10).hasValue());
    rig.linkUp();
    REQUIRE(rig.runRound());
    REQUIRE(rig.runRound());

    // With bitsAsWords the end (8190) is aligned up to 8192, past the limit.
    const ChunkInfo& planned = rig.session().plan().chunk(0);
    if (bitsAsWords) {
        CHECK(planned.request.op == Op::ReadWords);
        CHECK(planned.request.head == dev("M8176"));
        CHECK(planned.request.count == 1);
    } else {
        CHECK(planned.request.op == Op::ReadBits);
        CHECK(planned.request.head == dev("M8180"));
        CHECK(planned.request.count == 10);
    }

    for (uint32_t round = 1; round <= 3; ++round) {
        CAPTURE(round);
        const std::vector<Event> events = roundEvents(rig.events(), round);
        REQUIRE(events.size() == 2);
        REQUIRE(events[0].kind == OutputKind::Snapshot);
        REQUIRE(events[0].chunks.size() == 1);
        const ChunkInfo& c = events[0].chunks[0];
        if (bitsAsWords) {
            CHECK(c.state == ChunkState::Failed);
            CHECK(c.lastError.category == ErrorCategory::Plc);
            CHECK(c.lastError.plcCode == kOutOfRange);
            CHECK(c.lastError.abnormalCode == (is1e(combo) ? 0x10 : 0));
            CHECK(events[1].cycle.failedChunks == 1);
        } else {
            CHECK(c.state == ChunkState::Ok);
            CHECK(events[1].cycle.failedChunks == 0);
        }
    }

    const ValueStore& values = rig.session().values();
    if (bitsAsWords) {
        CHECK(values.state(dev("M8181")) == PointState::NoValue); // never read
    } else {
        CHECK(values.state(dev("M8181")) == PointState::Valid);
        CHECK(values.bit(dev("M8181")));
        CHECK_FALSE(values.bit(dev("M8180")));
    }
}

void int16(const Combo& combo) {
    int16Case(combo, true);
    int16Case(combo, false);
}

// ---- XYN: X/Y numbering for FX CPUs --------------------------------------------------

// The same write and read of X64..X71 (index 64: X100 octal, X40 hex) on every frame with the
// X/Y digits in hex and in octal: client and mock agree, the bits land on index 64.
void intXyn(const Combo& base) {
    for (XyNumbering digits : {XyNumbering::Hex, XyNumbering::Octal}) {
        Combo combo = base;
        combo.frame.xyNotation = digits;
        combo.frame.xyAsciiDigits = digits;
        INFO("digits ", digits == XyNumbering::Octal ? "octal" : "hex");
        Rig rig(combo);
        rig.linkUp();

        const ByteBuf bits = {1, 0, 1, 1, 0, 0, 1, 0};
        Expected<RequestId> wrote =
            rig.submit(Request::writeBits(dn(DeviceType::X, 64), view(bits)));
        REQUIRE(wrote.hasValue());
        const std::vector<Event> writeDone = rig.requestDones(wrote.value());
        REQUIRE(writeDone.size() == 1);
        CHECK(writeDone[0].error.ok());
        for (uint32_t i = 0; i < bits.size(); ++i) {
            CHECK(rig.mock().bit(dn(DeviceType::X, 64 + i)) == (bits[i] != 0));
        }
        CHECK_FALSE(rig.mock().bit(dn(DeviceType::X, 40)));
        CHECK_FALSE(rig.mock().bit(dn(DeviceType::X, 100)));

        Expected<RequestId> read = rig.submit(Request::readBits(dn(DeviceType::X, 64), 8));
        REQUIRE(read.hasValue());
        const std::vector<Event> readDone = rig.requestDones(read.value());
        REQUIRE(readDone.size() == 1);
        CHECK(readDone[0].error.ok());
        CHECK(readDone[0].payload == bits);

        // A word of bits: X64..X79 as one word, bit k of the word is point 64 + k.
        Expected<RequestId> word = rig.submit(Request::readWords(dn(DeviceType::X, 64), 1));
        REQUIRE(word.hasValue());
        const std::vector<Event> wordDone = rig.requestDones(word.value());
        REQUIRE(wordDone.size() == 1);
        CHECK(wordDone[0].error.ok());
        CHECK(wordDone[0].payload == (ByteBuf{0x4D, 0x00}));
    }
}

} // namespace

TEST_CASE("INT-XYN X/Y digits in hex and in octal, write and read back, on every frame") {
    INT_MATRIX(intXyn);
}

TEST_CASE("INT-01 ad-hoc word write then read, one RequestDone each") { INT_MATRIX(int01); }
TEST_CASE("INT-02 ad-hoc bit write then read") { INT_MATRIX(int02); }
TEST_CASE("INT-03 odd bit count write and read") { INT_MATRIX(int03); }
TEST_CASE("INT-07 read D0x2000 is split into chunkCount() requests") { INT_MATRIX(int07); }
TEST_CASE("INT-08 failRange gives a PLC error and the link stays healthy") { INT_MATRIX(int08); }
TEST_CASE("INT-10 round 1 silent, round 2 reports only what changed") { INT_MATRIX(int10); }
TEST_CASE("INT-11 mute during polling: Ethernet faults at once, serial retries then faults") {
    INT_MATRIX(int11);
}
TEST_CASE("INT-12 damaged response: Ethernet protocol-error fault, serial EOT and retry") {
    INT_MATRIX(int12);
}
TEST_CASE("INT-13 bitsAsWords on and off see identical values and changes") { INT_MATRIX(int13); }
TEST_CASE("INT-14 heartbeat writes 1,0,1,0,1 over five rounds") { INT_MATRIX(int14); }
TEST_CASE("INT-15 twenty fragmentation seeds give identical output sequences") {
    INT_MATRIX(int15);
}
TEST_CASE("INT-16 bitsAsWords past a device limit fails every round, bit reads succeed") {
    INT_MATRIX(int16);
}
