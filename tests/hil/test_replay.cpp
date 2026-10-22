// mc_replay_tests (SPEC-hil-capture.md "Replay tests", label `replay`): RPL-01..06.
//
//  - "RPL-sweep ..." replays every capture folder under the root (default tests/vectors/captured/,
//    hardware captures only; `--replay-root=<dir>` or MC_REPLAY_ROOT points it elsewhere, which is
//    what the HIL-04 end-to-end test does with the capture it has just written).
//  - "RPL-0n ... fixtures" run each check on the committed fixtures of tests/hil/fixtures/, which
//    are captures of examples/virtual_plc (not hardware) and keep the checks running on every
//    build.
//  - "RPL-0n negative ..." copy a fixture to the build tree, change one byte, one value or one
//  event,
//    and prove that the check fails and names the profile and the step.
//
// Std-only: no Qt, no network, no serial port.
#include "doctest/doctest.h"

#include "replay/replay.h"
#include "replay/replay_options.h"

#include <climits>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;
using namespace mc::replay;

namespace {

fs::path testsDir() { return fs::weakly_canonical(fs::path(MC_TESTS_SOURCE_DIR)); }
fs::path fixturesDir() { return testsDir() / "hil" / "fixtures"; }
fs::path scratchDir() { return fs::path(MC_REPLAY_SCRATCH_DIR); }

Env realEnv() {
    Env env;
    env.vectorsDir = testsDir() / "vectors";
    env.findingsFile = testsDir().parent_path() / "docs" / "hil" / "FINDINGS.md";
    return env;
}

std::string slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    std::string text = os.str();
    for (size_t at = text.find("\r\n"); at != std::string::npos; at = text.find("\r\n", at)) {
        text.erase(at, 1);
    }
    return text;
}

void spit(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

/// A copy of fixture @p id under the build tree, for a negative test to damage.
fs::path scratchCopy(const std::string& id, const std::string& name) {
    const fs::path dst = scratchDir() / name / id;
    fs::remove_all(dst);
    fs::create_directories(dst.parent_path());
    fs::copy(fixturesDir() / id, dst, fs::copy_options::recursive);
    return dst;
}

/// Replaces the first @p from after the line "# id: <recordId>" with @p to.
void editAfter(const fs::path& file, const std::string& recordId, const std::string& from,
               const std::string& to) {
    std::string text = slurp(file);
    const size_t anchor = text.find("# id: " + recordId + "\n");
    REQUIRE_MESSAGE(anchor != std::string::npos,
                    "no record " << recordId << " in " << file.string());
    const size_t at = text.find(from, anchor);
    REQUIRE_MESSAGE(at != std::string::npos, "no '" << from << "' after " << recordId);
    text.replace(at, from.size(), to);
    spit(file, text);
}

/// Replaces byte @p index (0-based) of the hex line of record @p recordId by @p newHex.
void editByte(const fs::path& file, const std::string& recordId, size_t index,
              const std::string& newHex) {
    std::string text = slurp(file);
    const size_t anchor = text.find("# id: " + recordId + "\n");
    REQUIRE_MESSAGE(anchor != std::string::npos,
                    "no record " << recordId << " in " << file.string());
    size_t line = text.find('\n', anchor) + 1;
    while (text[line] == '#') {
        line = text.find('\n', line) + 1;
    }
    const size_t lineEnd = text.find('\n', line);
    const size_t at = line + index * 3;
    REQUIRE_MESSAGE(at + 2 <= lineEnd, "record " << recordId << " has no byte " << index);
    text.replace(at, 2, newHex);
    spit(file, text);
}

bool hasFailure(const Report& r, const std::string& check, const std::string& step) {
    for (const Failure& f : r.failures) {
        if (f.check == check && f.step == step) {
            return true;
        }
    }
    return false;
}

/// The text of every failure, for a failing assertion's message.
std::string dump(const Report& r) {
    std::string t;
    for (const Failure& f : r.failures) {
        t += "\n  " + f.text();
    }
    return t;
}

void printNotes(const Report& r) {
    for (const std::string& n : r.notes) {
        std::cout << "note: " << n << "\n";
    }
    for (const Failure& f : r.knownDivergences) {
        std::cout << "known-divergence: " << f.text() << "\n";
    }
}

std::vector<fs::path> fixtureFolders() {
    const std::vector<fs::path> folders = captureFolders(fixturesDir());
    REQUIRE_MESSAGE(!folders.empty(), "no fixture under " << fixturesDir().string());
    return folders;
}

/// Runs one check on every fixture.
template <typename Check> void checkFixtures(const char* name, Check check) {
    for (const fs::path& dir : fixtureFolders()) {
        const Capture c = loadCapture(dir);
        Report r;
        check(c, r);
        printNotes(r);
        CHECK_MESSAGE(r.failures.empty(), name << " on fixture " << c.profile << dump(r));
        CHECK_MESSAGE(r.checks > 0, name << " on fixture " << c.profile << " made no comparison");
    }
}

} // namespace

// ---- the sweep: hardware captures, or whatever --replay-root points at
// ----------------------------

TEST_CASE("RPL-sweep: every capture folder under the root replays (RPL-01..06)") {
    const fs::path root =
        g_rootOption.empty() ? testsDir() / "vectors" / "captured" : fs::path(g_rootOption);
    const std::vector<fs::path> folders = captureFolders(root);
    if (folders.empty()) {
        std::cout << "no captures under " << root.string() << ": nothing to replay\n";
        return;
    }
    const Env env = realEnv();
    for (const fs::path& dir : folders) {
        try {
            const Capture c = loadCapture(dir);
            const Report r = checkAll(c, env);
            printNotes(r);
            std::cout << "replayed " << c.profile << ": " << r.checks << " comparisons, "
                      << r.failures.size() << " failures, " << r.knownDivergences.size()
                      << " known divergences\n";
            CHECK_MESSAGE(r.failures.empty(), "capture " << c.profile << dump(r));
            CHECK_MESSAGE(r.checks > 0, "capture " << c.profile << " made no comparison");
        } catch (const std::exception& e) {
            FAIL_CHECK("capture folder " << dir.filename().string()
                                         << " cannot be replayed: " << e.what());
        }
    }
}

TEST_CASE("RPL-sweep: an absent or empty root passes with a note") {
    const fs::path none = scratchDir() / "no_such_root";
    fs::remove_all(none);
    CHECK(captureFolders(none).empty());
    fs::create_directories(scratchDir() / "empty_root");
    CHECK(captureFolders(scratchDir() / "empty_root").empty());
}

// ---- the checks on the committed fixtures ------------------------------------------------------

TEST_CASE("RPL-01 every api request re-encodes from its metadata (fixtures)") {
    checkFixtures("RPL-01", [](const Capture& c, Report& r) { checkReencode(c, r); });
}

TEST_CASE("RPL-02 every response parses to its recorded outcome and values (fixtures)") {
    checkFixtures("RPL-02", [](const Capture& c, Report& r) { checkParse(c, r); });
}

TEST_CASE("RPL-03 the mock answers every captured request with the captured bytes (fixtures)") {
    checkFixtures("RPL-03", [](const Capture& c, Report& r) { checkMock(c, r); });
}

TEST_CASE("RPL-04 the Session replays the poll transcripts (fixtures)") {
    checkFixtures("RPL-04", [](const Capture& c, Report& r) { checkSession(c, r); });
}

TEST_CASE("RPL-05 write read-backs match and GV steps equal Appendix A (fixtures)") {
    const Env env = realEnv();
    checkFixtures("RPL-05", [&env](const Capture& c, Report& r) { checkSanity(c, env, r); });
}

TEST_CASE("RPL-06 divergences.txt entries have FINDINGS entries (fixtures carry none)") {
    const Env env = realEnv();
    for (const fs::path& dir : fixtureFolders()) {
        const Capture c = loadCapture(dir);
        Report r;
        checkDivergences(c, env, r);
        CHECK_MESSAGE(r.failures.empty(), c.profile << dump(r));
    }
}

TEST_CASE("RPL-sweep: the fixtures cover an Ethernet profile and carry the not-hardware note") {
    bool ethernet = false;
    for (const fs::path& dir : fixtureFolders()) {
        const Capture c = loadCapture(dir);
        ethernet = ethernet || c.transport == "tcp";
        CHECK_MESSAGE(
            c.meta.at("operator_note").find("virtual_plc fixture, not hardware") !=
                std::string::npos,
            c.profile << ": run.meta operator_note must say 'virtual_plc fixture, not hardware'");
    }
    CHECK(ethernet);
}

// ---- negative tests: one change, one failing check, named profile and step
// ----------------------------

TEST_CASE("RPL-01 negative: a changed byte of a request fails and names the profile and the step") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl01");
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-E-04", 15, "65"); // the device number D100 -> D101
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-01", "E-04"), dump(r));
    for (const Failure& f : r.failures) {
        if (f.check == "RPL-01") {
            CHECK(f.text().find("vplc-3e-bin") != std::string::npos);
            CHECK(f.text().find("E-04") != std::string::npos);
        }
    }
}

TEST_CASE("RPL-01 negative: a metadata count that differs from the frame fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl01_meta");
    editAfter(dir / "steps.vec", "CAP-vplc-3e-bin-E-04", "count: 3", "count: 4");
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-01", "E-04"), dump(r));
}

TEST_CASE("RPL-02 negative: a response that no longer parses to the recorded outcome fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl02");
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-E-01-R", 9, "59"); // end code 0000 -> 0059
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-02", "E-01"), dump(r));
    CHECK(r.of("RPL-02").front().text().find("vplc-3e-bin") != std::string::npos);
}

TEST_CASE("RPL-02 negative: a parsed value that differs from the recorded values fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl02_values");
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-E-04-R", 11, "09"); // words 1 2 3 -> 9 2 3
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-02", "E-04"), dump(r));
}

TEST_CASE("RPL-03 negative: a response the mock would not give fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl03");
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-E-06-R", 11,
             "09"); // D100 was written 1, the capture says 9
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-03", "E-06"), dump(r));
    CHECK(r.of("RPL-03").front().text().find("vplc-3e-bin") != std::string::npos);
    CHECK_FALSE(hasFailure(r, "RPL-02", "E-06"));
}

TEST_CASE("RPL-03 negative: an error code the mock does not use fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl03_error");
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-E-09-R", 9, "51"); // C059 -> C051
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-03", "E-09"), dump(r));
}

TEST_CASE("RPL-04 negative: a changed tx frame fails and names the profile and the poll step") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_tx");
    editByte(dir / "session.vec", "CAP-vplc-3e-bin-E-12-T0003", 15, "05");
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
    CHECK(r.of("RPL-04").front().text().find("vplc-3e-bin") != std::string::npos);
}

TEST_CASE("RPL-04 negative: a changed received value changes the events and fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_rx");
    editByte(dir / "session.vec", "CAP-vplc-3e-bin-E-12-T0008", 11, "77");
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
}

TEST_CASE("RPL-04 negative: a changed recorded event fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_event");
    editByte(dir / "session.vec", "CAP-vplc-3e-bin-E-12-E0009", 7,
             "55"); // the new value of one change
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
}

TEST_CASE("RPL-04 negative: a recorded cycleDone the replay does not produce fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_round");
    editByte(dir / "session.vec", "CAP-vplc-3e-bin-E-12-E0013", 1,
             "09"); // the round number of a cycleDone
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
}

TEST_CASE("RPL-05 negative: a read-back that differs from what was written fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl05_readback");
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-GV-01.2-R", 11, "96");
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-05", "GV-01"), dump(r));
    CHECK(r.of("RPL-05").front().text().find("vplc-3e-bin") != std::string::npos);
}

TEST_CASE("RPL-05 negative: a write with no read-back fails") {
    // The capture ends with the write of GV-01 (D100 x3): no later record reads the points.
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl05_noreadback");
    const std::string steps = slurp(dir / "steps.vec");
    const size_t readBack = steps.find("# id: CAP-vplc-3e-bin-GV-01.2\n");
    REQUIRE(readBack != std::string::npos);
    spit(dir / "steps.vec", steps.substr(0, readBack));
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-05", "GV-01"), dump(r));
}

TEST_CASE("RPL-05 negative: a GV request that differs from its Appendix A vector fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl05_gv");
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-GV-02", 17, "90"); // the D device code A8 -> 90
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-05", "GV-02"), dump(r));
    bool named = false;
    for (const Failure& f : r.of("RPL-05")) {
        named = named || f.text().find("V-3E-B-01") != std::string::npos;
    }
    CHECK(named);
}

TEST_CASE("RPL-06 negative: a divergence with no FINDINGS entry fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl06");
    spit(dir / "divergences.txt", "# step  finding\nE-04 F-999\n");
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-06", "E-04"), dump(r));
    CHECK(r.of("RPL-06").front().text().find("vplc-3e-bin") != std::string::npos);
}

TEST_CASE("RPL-06 negative: a divergence naming a step that is not in the capture fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl06_step");
    fs::create_directories(scratchDir() / "rpl06_step_findings");
    spit(scratchDir() / "rpl06_step_findings" / "FINDINGS.md", "### F-999 - a finding\n");
    Env env = realEnv();
    env.findingsFile = scratchDir() / "rpl06_step_findings" / "FINDINGS.md";
    spit(dir / "divergences.txt", "NOPE-01 F-999\n");
    const Report r = checkAll(loadCapture(dir), env);
    CHECK_MESSAGE(hasFailure(r, "RPL-06", "NOPE-01"), dump(r));
}

TEST_CASE("RPL-06 a tagged step reports known-divergence and does not fail the run") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl06_known");
    fs::create_directories(scratchDir() / "rpl06_known_findings");
    spit(scratchDir() / "rpl06_known_findings" / "FINDINGS.md",
         "### F-999 - a finding\n- Status: open\n");
    Env env = realEnv();
    env.findingsFile = scratchDir() / "rpl06_known_findings" / "FINDINGS.md";
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-E-04", 9, "11"); // would fail RPL-01
    spit(dir / "divergences.txt", "E-04 F-999\n");
    const Report r = checkAll(loadCapture(dir), env);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(!r.knownDivergences.empty());
    // untagged damage still fails
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-E-07", 15, "70");
    const Report r2 = checkAll(loadCapture(dir), env);
    CHECK(!r2.failures.empty());
}

TEST_CASE("RPL-sweep: a damaged capture folder is reported, not ignored") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "damaged");
    fs::remove(dir / "steps.vec");
    CHECK_THROWS(loadCapture(dir));
    const fs::path dir2 = scratchCopy("vplc-3e-bin", "damaged_meta");
    std::string meta = slurp(dir2 / "run.meta");
    const size_t at = meta.find("frame.pc: ");
    REQUIRE(at != std::string::npos);
    meta.erase(at, meta.find('\n', at) + 1 - at);
    spit(dir2 / "run.meta", meta);
    try {
        (void)loadCapture(dir2);
        FAIL("a run.meta without frame.pc must not load");
    } catch (const std::exception& e) {
        CHECK(std::string(e.what()).find("frame.pc") != std::string::npos);
    }
}

TEST_CASE("RPL-01 negative (serial): a changed byte of a 3C request fails and names the profile "
          "and the step") {
    const fs::path dir = scratchCopy("vplc-3c-f4", "rpl01_serial");
    editByte(dir / "steps.vec", "CAP-vplc-3c-f4-E-04", 8, "31"); // the station number field
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-01", "E-04"), dump(r));
    CHECK(r.of("RPL-01").front().text().find("vplc-3c-f4") != std::string::npos);
}

TEST_CASE("RPL-04 negative (serial): a changed tx frame of the 3C poll fails") {
    const fs::path dir = scratchCopy("vplc-3c-f4", "rpl04_serial");
    editByte(dir / "session.vec", "CAP-vplc-3c-f4-E-12-T0004", 8, "31");
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
}

// ---- recorded poll inputs, verdict, override
// ---------------------------------------------------------------

namespace {

/// Removes every record of kind input from a session.vec text.
std::string withoutInputs(const std::string& text) {
    std::string out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find("\n\n", pos);
        end = end == std::string::npos ? text.size() : end + 2;
        const std::string block = text.substr(pos, end - pos);
        if (block.find("kind: input") == std::string::npos) {
            out += block;
        }
        pos = end;
    }
    return out;
}

/// Adds @p deltaNs to every `t_ns:` of at least @p fromNs.
void shiftTimes(const fs::path& file, long long fromNs, long long deltaNs) {
    std::string text = slurp(file);
    std::string out;
    size_t pos = 0;
    for (;;) {
        const size_t at = text.find("t_ns: ", pos);
        if (at == std::string::npos) {
            out += text.substr(pos);
            break;
        }
        const size_t numStart = at + 6;
        size_t numEnd = numStart;
        while (numEnd < text.size() && text[numEnd] >= '0' && text[numEnd] <= '9') {
            ++numEnd;
        }
        long long t = std::stoll(text.substr(numStart, numEnd - numStart));
        if (t >= fromNs) {
            t += deltaNs;
        }
        out += text.substr(pos, numStart - pos) + std::to_string(t);
        pos = numEnd;
    }
    spit(file, out);
}

} // namespace

TEST_CASE("RPL-04 uses the recorded inputs, and falls back to the rebuilt ones without them") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_fallback");
    const std::string full = slurp(dir / "session.vec");
    REQUIRE(full.find("kind: input") != std::string::npos);
    Report with;
    checkSession(loadCapture(dir), with);
    CHECK_MESSAGE(with.failures.empty(), dump(with));
    spit(dir / "session.vec", withoutInputs(full));
    Report without;
    checkSession(loadCapture(dir), without);
    CHECK_MESSAGE(without.failures.empty(), "fallback: " << dump(without));
    CHECK(without.checks > 0);
}

TEST_CASE("RPL-04 negative: a changed recorded ad-hoc write fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_input_value");
    editByte(dir / "session.vec", "CAP-vplc-3e-bin-E-12-I0005", 9,
             "08"); // the written value 7 -> 8
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
    CHECK(r.of("RPL-04").front().text().find("vplc-3e-bin") != std::string::npos);
}

TEST_CASE("RPL-04 negative: a recorded input that is missing fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_input_missing");
    std::string text = slurp(dir / "session.vec");
    const size_t at = text.find("# id: CAP-vplc-3e-bin-E-12-I0005\n");
    REQUIRE(at != std::string::npos);
    text.erase(at, text.find("\n\n", at) + 2 - at);
    spit(dir / "session.vec", text);
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
}

TEST_CASE("RPL-04 a response that arrives after the deadline, with the live run unfaulted, is not "
          "a fault") {
    // The 3E fixture's response timeout is 5000 ms. A stall of the event loop made a response late
    // by more than that while the live Session still handled it before its timer: the capture holds
    // no linkFault.
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_stall");
    const long long delta = 6000000000LL;
    // the last response of the transcript and everything after it come 6 s late
    const std::string text = slurp(dir / "session.vec");
    const size_t lastRx = text.rfind("kind: rx  t_ns: ");
    REQUIRE(lastRx != std::string::npos);
    const long long fromNs = std::stoll(text.substr(lastRx + 16));
    const size_t lastCycle = text.rfind("event: cycleDone");
    REQUIRE(lastCycle != std::string::npos);
    const size_t idAt = text.rfind("# id: ", lastCycle);
    const std::string cycleId = text.substr(idAt + 6, text.find('\n', idAt) - idAt - 6);
    shiftTimes(dir / "session.vec", fromNs, delta);
    // the last round's duration grows by the stall: cycleDone durationMs is the u32 at byte 13
    editByte(dir / "session.vec", cycleId, 13, "71");
    editByte(dir / "session.vec", cycleId, 14, "17");
    Report r;
    checkSession(loadCapture(dir), r);
    // The Session runs 6 s over its round, so it starts the next round at once (FixedRate), a frame
    // the capture ends before: that one extra frame is the only difference. No fault, nothing
    // missing.
    REQUIRE_MESSAGE(r.failures.size() == 1, dump(r));
    CHECK_MESSAGE(
        r.failures.front().message.find("tx frame #22: the replay sent a frame the capture "
                                        "does not have") == 0,
        r.failures.front().text());
}

TEST_CASE(
    "RPL-02 a step whose verdict is not passed is a finding; tagged, it is a known divergence") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl02_verdict");
    editAfter(dir / "steps.vec", "CAP-vplc-3e-bin-E-04-R", "verdict: passed", "verdict: failed");
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-02", "E-04"), dump(r));
    CHECK(r.of("RPL-02").front().text().find("vplc-3e-bin") != std::string::npos);
    CHECK(r.of("RPL-02").front().text().find("failed") != std::string::npos);
    // the parse-outcome check is still made: an ok outcome that no longer parses fails as before
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-E-01-R", 9, "59");
    const Report r2 = checkAll(loadCapture(dir), realEnv());
    CHECK(hasFailure(r2, "RPL-02", "E-01"));
    // tagged with a FINDINGS entry: known divergence
    const fs::path dir2 = scratchCopy("vplc-3e-bin", "rpl02_verdict_tagged");
    editAfter(dir2 / "steps.vec", "CAP-vplc-3e-bin-E-04-R", "verdict: passed", "verdict: diverged");
    fs::create_directories(scratchDir() / "rpl02_findings");
    spit(scratchDir() / "rpl02_findings" / "FINDINGS.md", "### F-998 - a finding\n");
    spit(dir2 / "divergences.txt", "E-04 F-998\n");
    Env env = realEnv();
    env.findingsFile = scratchDir() / "rpl02_findings" / "FINDINGS.md";
    const Report r3 = checkAll(loadCapture(dir2), env);
    CHECK_MESSAGE(r3.failures.empty(), dump(r3));
    CHECK(!r3.knownDivergences.empty());
}

TEST_CASE("RPL-01 skips a record that ran with an override and says so") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl01_override");
    editAfter(dir / "steps.vec", "CAP-vplc-3e-bin-E-04", "via: api",
              "via: api  override: stationNo=+1");
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-E-04", 15,
             "65"); // would fail RPL-01 without the override
    Report r;
    checkReencode(loadCapture(dir), r);
    CHECK_FALSE(hasFailure(r, "RPL-01", "E-04"));
    bool noted = false;
    for (const std::string& n : r.notes) {
        noted = noted || n.find("E-04") != std::string::npos;
    }
    CHECK(noted);
}

// ---- a poll that faults, reconnects and restarts (the shape of catalogue G7-06)
// -----------------------------

namespace {

/// The blocks of a .vec text: each runs from a "# id:" line to the blank line after its bytes.
std::vector<std::string> blocksOf(const std::string& text) {
    std::vector<std::string> blocks;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find("\n\n", pos);
        end = end == std::string::npos ? text.size() : end + 2;
        blocks.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return blocks;
}

std::string idOf(const std::string& block) {
    const size_t at = block.find("# id: ");
    return at == std::string::npos ? std::string()
                                   : block.substr(at + 6, block.find('\n', at) - at - 6);
}

/// The id of the @p nth (0-based) record of @p file whose text contains @p needle.
std::string recordWith(const fs::path& file, const std::string& needle, int nth = 0) {
    int seen = 0;
    for (const std::string& block : blocksOf(slurp(file))) {
        if (block.find(needle) != std::string::npos && seen++ == nth) {
            return idOf(block);
        }
    }
    FAIL("no record " << nth << " containing '" << needle << "' in " << file.string());
    return std::string();
}

/// Removes the record @p recordId from @p file.
void removeRecord(const fs::path& file, const std::string& recordId) {
    std::string out;
    bool found = false;
    for (const std::string& block : blocksOf(slurp(file))) {
        if (idOf(block) == recordId) {
            found = true;
        } else {
            out += block;
        }
    }
    REQUIRE_MESSAGE(found, "no record " << recordId << " in " << file.string());
    spit(file, out);
}

/// Adds @p delta to byte @p index of the hex line of record @p recordId.
void bumpByte(const fs::path& file, const std::string& recordId, size_t index, int delta) {
    std::string text = slurp(file);
    const size_t anchor = text.find("# id: " + recordId + "\n");
    REQUIRE_MESSAGE(anchor != std::string::npos,
                    "no record " << recordId << " in " << file.string());
    size_t line = text.find('\n', anchor) + 1;
    while (text[line] == '#') {
        line = text.find('\n', line) + 1;
    }
    const size_t at = line + index * 3;
    const int value = std::stoi(text.substr(at, 2), nullptr, 16);
    char hex[8];
    std::snprintf(hex, sizeof hex, "%02X", (value + delta) & 0xFF);
    text.replace(at, 2, hex);
    spit(file, text);
}

/// Sets the run.meta value of @p key (a key that is already there, not the first line).
void setMeta(const fs::path& dir, const std::string& key, const std::string& value) {
    std::string text = slurp(dir / "run.meta");
    const std::string needle = "\n" + key + ": ";
    const size_t at = text.find(needle);
    REQUIRE_MESSAGE(at != std::string::npos, "no key " << key << " in run.meta");
    const size_t from = at + needle.size();
    text.replace(from, text.find('\n', from) - from, value);
    spit(dir / "run.meta", text);
}

/// The failures of @p r that belong to check @p check, one text per line.
std::string failuresOf(const Report& r, const std::string& check) {
    std::string t;
    for (const Failure& f : r.of(check)) {
        t += f.text() + "\n";
    }
    return t;
}

// The hex lines of the link states: Connected, and Disconnected for a peer-closed link.
const char* const kConnected = "\n05 02 00\n";
const char* const kDisconnectedPeerClosed = "\n05 00 02\n";

} // namespace

TEST_CASE("RPL-04 a poll in which the link faults on a timeout, reconnects and restarts replays") {
    // vplc-3e-bin-fault: virtual_plc behind a proxy that drops the responses of one connection. The
    // tool reports the poll as passed; session.vec holds the linkFault, the reconnect and the
    // restarted rounds.
    const fs::path dir = scratchCopy("vplc-3e-bin-fault", "rpl04_fault");
    REQUIRE(slurp(dir / "session.vec").find("event: linkFault") != std::string::npos);
    REQUIRE_FALSE(recordWith(dir / "session.vec", kConnected, 1).empty()); // the reconnect
    Report r;
    checkSession(loadCapture(dir), r);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks > 50);
    const Report all = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(all.failures.empty(), dump(all));
}

TEST_CASE("RPL-04 a poll whose peer closes the link while a write is outstanding replays") {
    // vplc-3e-bin-drop: the proxy closes the connection on the ad-hoc write; the Session finishes
    // the outstanding write (requestFinished) before the link goes down, then the poll restarts.
    const fs::path dir = scratchCopy("vplc-3e-bin-drop", "rpl04_drop");
    REQUIRE(slurp(dir / "session.vec").find(kDisconnectedPeerClosed) != std::string::npos);
    Report r;
    checkSession(loadCapture(dir), r);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks > 30);
}

TEST_CASE("RPL-04 negative: a linkFault event that records another kind of fault fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin-fault", "rpl04_fault_kind");
    const std::string fault = recordWith(dir / "session.vec", "event: linkFault");
    bumpByte(dir / "session.vec", fault, 1, 1); // Timeout -> ProtocolError
    Report r;
    checkSession(loadCapture(dir), r);
    REQUIRE_MESSAGE(hasFailure(r, "RPL-04", "L-02"), dump(r));
    CHECK(r.of("RPL-04").front().text().find("vplc-3e-bin-fault") != std::string::npos);
}

TEST_CASE("RPL-04 negative: a capture that lost its reconnect fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin-fault", "rpl04_fault_noreconnect");
    removeRecord(dir / "session.vec", recordWith(dir / "session.vec", kConnected, 1));
    Report r;
    checkSession(loadCapture(dir), r);
    REQUIRE_MESSAGE(hasFailure(r, "RPL-04", "L-02"), dump(r));
}

TEST_CASE("RPL-04 negative: a changed ad-hoc write after the restart fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin-fault", "rpl04_fault_input");
    const std::string second = recordWith(dir / "session.vec", "input: write", 1);
    bumpByte(dir / "session.vec", second, 9, 1); // the written value 9 -> 10
    Report r;
    checkSession(loadCapture(dir), r);
    CHECK_MESSAGE(hasFailure(r, "RPL-04", "L-02"), dump(r));
}

TEST_CASE(
    "RPL-04 negative: a restarted round that the capture numbers on from the old ones fails") {
    // Round numbering starts again at 1 after the reconnect; a capture that goes on counting is
    // damaged.
    const fs::path dir = scratchCopy("vplc-3e-bin-fault", "rpl04_fault_round");
    const std::string cycle = recordWith(dir / "session.vec", "round: 1  duration_ms", 1);
    bumpByte(dir / "session.vec", cycle, 1, 6); // the first round after the restart: 1 -> 7
    Report r;
    checkSession(loadCapture(dir), r);
    CHECK_MESSAGE(hasFailure(r, "RPL-04", "L-02"), dump(r));
}

TEST_CASE("RPL-04 negative: an outstanding write whose requestFinished is missing fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin-drop", "rpl04_drop_norequest");
    const fs::path file = dir / "session.vec";
    // the record just before the peer-closed link state is the requestFinished of the outstanding
    // write
    std::string previous;
    std::string wanted;
    for (const std::string& block : blocksOf(slurp(file))) {
        if (block.find(kDisconnectedPeerClosed) != std::string::npos) {
            wanted = previous;
            break;
        }
        previous = block;
    }
    REQUIRE_MESSAGE(wanted.find("event: requestFinished") != std::string::npos, wanted);
    removeRecord(file, idOf(wanted));
    Report r;
    checkSession(loadCapture(dir), r);
    CHECK_MESSAGE(hasFailure(r, "RPL-04", "P-02"), dump(r));
}

TEST_CASE("RPL-04 negative: a capture that lost the peer-closed link state fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin-drop", "rpl04_drop_nolink");
    removeRecord(dir / "session.vec", recordWith(dir / "session.vec", kDisconnectedPeerClosed));
    Report r;
    checkSession(loadCapture(dir), r);
    CHECK_MESSAGE(hasFailure(r, "RPL-04", "P-02"), dump(r));
}

// ---- the Session's deadlines against the captured times
// -------------------------------------------------

TEST_CASE("RPL-04 negative: a changed cycle interval in run.meta fails with both times") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_interval");
    setMeta(dir, "session.cycleIntervalMs", "200"); // captured with 100
    Report r;
    checkSession(loadCapture(dir), r);
    REQUIRE_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
    const std::string text = r.of("RPL-04").front().text();
    CHECK(text.find("vplc-3e-bin") != std::string::npos);
    CHECK_MESSAGE(text.find("timer is due at") != std::string::npos, text);
    CHECK_MESSAGE(text.find("cycle interval") != std::string::npos, text);
    CHECK(r.of("RPL-04").size() == 1); // one clear message, not a cascade
}

TEST_CASE("RPL-04 negative: a changed cycle interval in a poll that faulted fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin-fault", "rpl04_interval_fault");
    setMeta(dir, "session.cycleIntervalMs", "100"); // captured with 50
    Report r;
    checkSession(loadCapture(dir), r);
    CHECK_MESSAGE(hasFailure(r, "RPL-04", "L-02"), dump(r));
}

TEST_CASE("RPL-04 negative: a changed response timeout in run.meta fails") {
    // The fault of vplc-3e-bin-fault is the timeout of a round: the capture shows it 400 ms after
    // the request.
    const fs::path dir = scratchCopy("vplc-3e-bin-fault", "rpl04_timeout");
    setMeta(dir, "frame.timeoutMs", "800");
    Report r;
    checkSession(loadCapture(dir), r);
    REQUIRE_MESSAGE(hasFailure(r, "RPL-04", "L-02"), dump(r));
    CHECK_MESSAGE(r.of("RPL-04").front().text().find("timer is due at") != std::string::npos,
                  dump(r));
}

TEST_CASE("RPL-04 negative (serial): a changed cycle interval of the 3C poll fails") {
    const fs::path dir = scratchCopy("vplc-3c-f4", "rpl04_interval_serial");
    setMeta(dir, "session.cycleIntervalMs", "400"); // captured with 100
    Report r;
    checkSession(loadCapture(dir), r);
    CHECK_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
}

TEST_CASE("RPL-04 a tick the live run made late is replayed at its captured time") {
    // Shifting every stamp from the start of round 3 on by 10 ms is a capture of a run whose timer
    // for that round fired 10 ms late (a loaded machine). The round's recorded duration is
    // unchanged, because the round began when the late tick fired. Replayed at the deadline instead
    // of the captured time, the round would start 10 ms early and last 10 ms too long.
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_late_tick");
    const fs::path file = dir / "session.vec";
    const std::string round2 = recordWith(file, "round: 2  duration_ms");
    // the first frame the Session sends after the cycleDone of round 2 starts round 3
    const std::vector<std::string> blocks = blocksOf(slurp(file));
    int seqOfRound2 = -1;
    for (const std::string& block : blocks) {
        if (idOf(block) == round2) {
            seqOfRound2 = std::stoi(block.substr(block.find("seq: ") + 5));
        }
    }
    REQUIRE(seqOfRound2 > 0);
    int firstSeq = INT_MAX;
    long long fromNs = -1;
    for (const std::string& block : blocks) {
        if (block.find("kind: tx") == std::string::npos) {
            continue;
        }
        const int seq = std::stoi(block.substr(block.find("seq: ") + 5));
        if (seq > seqOfRound2 && seq < firstSeq) {
            firstSeq = seq;
            fromNs = std::stoll(block.substr(block.find("t_ns: ") + 6));
        }
    }
    REQUIRE(fromNs > 0);
    shiftTimes(file, fromNs, 10000000LL);
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(r.failures.empty(), dump(r));
}

TEST_CASE("RPL-04 a cycle duration may differ by five milliseconds, not by ten") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_duration");
    const std::string cycle = recordWith(dir / "session.vec", "round: 2  duration_ms");
    bumpByte(dir / "session.vec", cycle, 13, 10); // the duration in ms (1) -> 11
    Report r;
    checkSession(loadCapture(dir), r);
    REQUIRE_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
    CHECK_MESSAGE(failuresOf(r, "RPL-04").find("duration") != std::string::npos, dump(r));
}

// ---- a capture folder that lost a file
// ----------------------------------------------------------------------

TEST_CASE("RPL-04 negative: a capture without session.vec fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_nosession");
    fs::remove(dir / "session.vec");
    Report r;
    checkSession(loadCapture(dir), r);
    REQUIRE_MESSAGE(!r.failures.empty(), "a missing session.vec must fail");
    CHECK(r.of("RPL-04").front().text().find("session.vec is missing") != std::string::npos);
    CHECK(r.of("RPL-04").front().text().find("vplc-3e-bin") != std::string::npos);
}

TEST_CASE(
    "RPL-04 negative: a poll step listed in run.meta with no transcript in session.vec fails") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_emptysession");
    {
        std::ofstream meta(dir / "run.meta", std::ios::binary | std::ios::app);
        meta << "polls: E-12\n";
    }
    Report listedAndPresent;
    checkSession(loadCapture(dir), listedAndPresent);
    CHECK_MESSAGE(listedAndPresent.failures.empty(), dump(listedAndPresent));
    spit(dir / "session.vec", "");
    Report r;
    checkSession(loadCapture(dir), r);
    REQUIRE_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
    CHECK(r.of("RPL-04").front().text().find("vplc-3e-bin") != std::string::npos);
    CHECK(r.of("RPL-04").front().text().find("no transcript") != std::string::npos);
}

TEST_CASE("RPL-04 an empty session.vec of a capture that lists no poll step is a note") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl04_nopolls");
    spit(dir / "session.vec", "");
    Report r;
    checkSession(loadCapture(dir), r);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    REQUIRE(!r.notes.empty());
    CHECK(r.notes.front().find("no poll transcript") != std::string::npos);
}

TEST_CASE(
    "RPL-sweep negative: a capture folder without run.meta is found and reported, not skipped") {
    const fs::path dir = scratchCopy("vplc-3e-bin", "no_run_meta");
    fs::remove(dir / "run.meta");
    const std::vector<fs::path> found = captureFolders(dir.parent_path());
    REQUIRE_MESSAGE(found.size() == 1, "the damaged folder must be listed");
    try {
        (void)loadCapture(found.front());
        FAIL("a capture folder without run.meta must not load");
    } catch (const std::exception& e) {
        CHECK(std::string(e.what()).find("run.meta") != std::string::npos);
    }
}

TEST_CASE("RPL-sweep: a sub-folder that holds no capture file is not a capture") {
    // the fixtures folder also holds the plans, the profiles and the helper that made the captures
    for (const fs::path& f : captureFolders(fixturesDir())) {
        CHECK(f.filename() != "plans");
        CHECK(f.filename() != "profiles");
        CHECK(f.filename() != "make");
    }
}

// ---- RPL-06 and FINDINGS.md
// ------------------------------------------------------------------------------------

TEST_CASE("RPL-06 negative: a heading inside an HTML comment of FINDINGS.md is not an entry") {
    // docs/hil/FINDINGS.md keeps its entry template in a comment, with a heading "### F-001"
    // inside.
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl06_comment");
    fs::create_directories(scratchDir() / "rpl06_comment_findings");
    const fs::path findings = scratchDir() / "rpl06_comment_findings" / "FINDINGS.md";
    spit(findings,
         "## Findings\n\n<!--\n### F-001 - a template\n- Status: open\n-->\n\n_None yet._\n");
    Env env = realEnv();
    env.findingsFile = findings;
    spit(dir / "divergences.txt", "E-04 F-001\n");
    const Report r = checkAll(loadCapture(dir), env);
    REQUIRE_MESSAGE(hasFailure(r, "RPL-06", "E-04"), dump(r));
    CHECK(r.of("RPL-06").front().text().find("F-001") != std::string::npos);
    // the same id as a real heading, after the comment, is an entry
    spit(findings,
         "<!--\n### F-001 - a template\n-->\n\n### F-001 - the finding\n- Status: open\n");
    const Report ok = checkAll(loadCapture(dir), env);
    CHECK_MESSAGE(!hasFailure(ok, "RPL-06", "E-04"), dump(ok));
}

// ---- RPL-05 against Appendix A
// -------------------------------------------------------------------------------

TEST_CASE(
    "RPL-05 negative: a GV response whose bytes differ from Appendix A fails with the same data") {
    // GV-02 reads D100 x3 and gets the values of the response of vector V-3E-B-01. Changing the
    // network number of the response header leaves the parsed values alone (the route is not
    // checked), so only the comparison with the bytes of the vector sees it.
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl05_gv_response");
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-GV-02-R", 3, "05");
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-05", "GV-02-R"), dump(r));
    CHECK(r.of("RPL-05").front().text().find("V-3E-B-02") != std::string::npos);
    CHECK_FALSE(hasFailure(r, "RPL-02", "GV-02"));
}

// ---- X/Y numbering of an FX capture: run.meta carries frame.xyNotation and
// frame.xyAsciiDigits; a capture without them is hex (every other fixture).

namespace {

void replaceInFile(const fs::path& file, const std::string& from, const std::string& to) {
    std::string text = slurp(file);
    const size_t at = text.find(from);
    REQUIRE_MESSAGE(at != std::string::npos, "no '" << from << "' in " << file.string());
    text.replace(at, from.size(), to);
    spit(file, text);
}

} // namespace

TEST_CASE("XYN-R1 the FX fixture (octal X/Y, octal digits in 3E ASCII) replays green") {
    const fs::path dir = fixturesDir() / "vplc-fx-3e-ascii-oct";
    const Capture c = loadCapture(dir);
    CHECK(c.frame.xyNotation == mc::XyNumbering::Octal);
    CHECK(c.frame.xyAsciiDigits == mc::XyNumbering::Octal);
    bool y10 = false;
    for (const Record& rec : c.records) {
        y10 = y10 || rec.device == "Y10";
    }
    CHECK(y10);
    const Report r = checkAll(c, realEnv());
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks > 0);
    // The other fixtures have no such keys and stay hex.
    const Capture hex = loadCapture(fixturesDir() / "vplc-3e-bin");
    CHECK(hex.frame.xyNotation == mc::XyNumbering::Hex);
    CHECK(hex.frame.xyAsciiDigits == mc::XyNumbering::Hex);
}

TEST_CASE("XYN-R2 negative: the FX capture read as hex fails") {
    // keys removed: Y10 would be index 16 and the digits would be read as hex
    const fs::path dir = scratchCopy("vplc-fx-3e-ascii-oct", "xyn_nokeys");
    replaceInFile(dir / "run.meta", "frame.xyAsciiDigits: Octal\n", "");
    replaceInFile(dir / "run.meta", "frame.xyNotation: Octal\n", "");
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(!r.failures.empty(), "a capture replayed as hex must not pass");
}

TEST_CASE("XYN-R3 negative: octal text with hex digits in the frame fails") {
    const fs::path dir = scratchCopy("vplc-fx-3e-ascii-oct", "xyn_digits");
    replaceInFile(dir / "run.meta", "frame.xyAsciiDigits: Octal", "frame.xyAsciiDigits: Hex");
    const Report r = checkAll(loadCapture(dir), realEnv());
    const bool found = hasFailure(r, "RPL-01", "F-02") || hasFailure(r, "RPL-03", "F-02");
    CHECK_MESSAGE(found, dump(r));
}

TEST_CASE("XYN-R4 negative: a damaged digit of the Y10 request fails") {
    const fs::path dir = scratchCopy("vplc-fx-3e-ascii-oct", "xyn_damaged");
    // the octal digit "1" of Y*000010 (byte 36 of the ASCII frame) becomes "2": Y20
    editByte(dir / "steps.vec", "CAP-vplc-fx-3e-ascii-oct-F-02", 36, "32");
    const Report r = checkAll(loadCapture(dir), realEnv());
    const bool found = hasFailure(r, "RPL-01", "F-02") || hasFailure(r, "RPL-03", "F-02");
    CHECK_MESSAGE(found, dump(r));
}

TEST_CASE("XYN-R5 negative: a request with a non-octal X/Y digit is reported as undecodable") {
    const fs::path dir = scratchCopy("vplc-fx-3e-ascii-oct", "xyn_nodecode");
    // the octal digit "1" of Y*000010 (byte 36) becomes "8": the mock cannot read the device
    editByte(dir / "steps.vec", "CAP-vplc-fx-3e-ascii-oct-F-02", 36, "38");
    const Report r = checkAll(loadCapture(dir), realEnv());
    REQUIRE_MESSAGE(hasFailure(r, "RPL-01", "F-02"), dump(r));
    for (const Failure& f : r.of("RPL-01")) {
        if (f.step == "F-02") {
            const std::string text = f.text();
            CHECK_MESSAGE(text.find("cannot decode") != std::string::npos, text);
            CHECK_MESSAGE(text.find("asks for") == std::string::npos, text);
        }
    }
}

TEST_CASE("XYN-R6 the device_end of an octal capture is read in octal") {
    const Capture c = loadCapture(fixturesDir() / "vplc-fx-3e-ascii-oct");
    uint32_t xEnd = 0;
    uint32_t yEnd = 0;
    uint32_t dEnd = 0;
    for (const auto& end : c.deviceEnd) {
        xEnd = end.first == mc::DeviceType::X ? end.second : xEnd;
        yEnd = end.first == mc::DeviceType::Y ? end.second : yEnd;
        dEnd = end.first == mc::DeviceType::D ? end.second : dEnd;
    }
    CHECK(xEnd == 1023u); // "1777" octal, not 0x1777
    CHECK(yEnd == 1023u);
    CHECK(dEnd == 12287u); // D stays decimal

    // The declared end reaches the mock: X10 octal (index 8) as the last input makes the capture's
    // read of X10 x8 run past it, which the mock answers with an error; read as 0x10 the end would
    // be index 16 and nothing would fail.
    const fs::path dir = scratchCopy("vplc-fx-3e-ascii-oct", "xyn_deviceend");
    replaceInFile(dir / "run.meta", "device_end: X=1777 Y=1777", "device_end: X=10 Y=10");
    const Capture shortEnd = loadCapture(dir);
    CHECK(shortEnd.deviceEnd.front().second == 8u);
    const Report r = checkAll(shortEnd, realEnv());
    CHECK_MESSAGE(!r.failures.empty(), "a read past the declared end must not replay green");
}

// ---- replay checker rules: RPL-05 read-back chain and per-profile GV, RPL-02 overrides, RPL-04
// duration tolerance. Each case builds a small capture folder from a fixture copy: the steps.vec is
// replaced by the records the case needs, so the case sees one rule and nothing else.

namespace {

std::string hex2(unsigned v) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02X", v & 0xFFu);
    return buf;
}

std::string hexNoPad(unsigned v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%X", v);
    return buf;
}

/// One request/response pair of steps.vec, in the layout the tool writes.
std::string stepPair(const std::string& profile, const std::string& step,
                     const std::string& sourceExtra, const std::string& frameText,
                     const std::string& op, const std::string& device, int count,
                     const std::string& via, const std::string& requestHex,
                     const std::string& responseHex, const std::string& expect,
                     const std::string& overrideText = std::string()) {
    const std::string id = "CAP-" + profile + "-" + step;
    const std::string over = overrideText.empty() ? std::string() : "  override: " + overrideText;
    std::string t;
    t += "# id: " + id + "\n";
    t += "# source: plc  profile: " + profile + "  step: " + step + sourceExtra + over + "\n";
    t += "# frame: " + frameText + "  op: " + op + "  device: " + device +
         "  count: " + std::to_string(count) + "  via: " + via + "\n";
    t += "# kind: request\n" + requestHex + "\n\n";
    t += "# id: " + id + "-R\n";
    t += "# kind: response  of: " + id + over + "\n";
    t += "# outcome: ok  expect: " + expect + "  verdict: passed\n";
    t += "# ttfb_ms: 1.000  rx_ms: 0.000  rtt_ms: 1.000\n" + responseHex + "\n\n";
    return t;
}

// ---- 3E binary records of vplc-3e-bin (hand-built from the frame layout of the fixture)

std::string devHead(unsigned number) { return hex2(number) + " " + hex2(number >> 8) + " 00"; }

const char* const kWriteOk3E = "D0 00 00 FF FF 03 00 02 00 00 00";

/// Write of the word D<d> with the value @p v.
std::string writeWord(const std::string& step, unsigned d, unsigned v) {
    return stepPair("vplc-3e-bin", step, "", "3E  code: Binary", "WriteWords",
                    "D" + std::to_string(d), 1, "api",
                    "50 00 00 FF FF 03 00 0E 00 10 00 01 14 00 00 " + devHead(d) + " A8 01 00 " +
                        hex2(v) + " " + hex2(v >> 8),
                    kWriteOk3E, "ok");
}

/// Read of the word D<d> that returns @p v.
std::string readWord(const std::string& step, unsigned d, unsigned v) {
    return stepPair(
        "vplc-3e-bin", step, "", "3E  code: Binary", "ReadWords", "D" + std::to_string(d), 1, "api",
        "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 " + devHead(d) + " A8 01 00",
        "D0 00 00 FF FF 03 00 04 00 00 00 " + hex2(v) + " " + hex2(v >> 8), "words " + hexNoPad(v));
}

/// Write of the bit M<m> with @p bit.
std::string writeBit(const std::string& step, unsigned m, int bit) {
    return stepPair("vplc-3e-bin", step, "", "3E  code: Binary", "WriteBits",
                    "M" + std::to_string(m), 1, "api",
                    "50 00 00 FF FF 03 00 0D 00 10 00 01 14 01 00 " + devHead(m) + " 90 01 00 " +
                        (bit != 0 ? "10" : "00"),
                    kWriteOk3E, "ok");
}

/// Read of the bit M<m> that returns @p bit.
std::string readBit(const std::string& step, unsigned m, int bit) {
    return stepPair("vplc-3e-bin", step, "", "3E  code: Binary", "ReadBits",
                    "M" + std::to_string(m), 1, "api",
                    "50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 " + devHead(m) + " 90 01 00",
                    std::string("D0 00 00 FF FF 03 00 03 00 00 00 ") + (bit != 0 ? "10" : "00"),
                    "bits " + std::to_string(bit));
}

/// A copy of fixture vplc-3e-bin whose steps.vec holds only @p steps.
fs::path readBackCapture(const std::string& name, const std::string& steps) {
    const fs::path dir = scratchCopy("vplc-3e-bin", name);
    spit(dir / "steps.vec", steps);
    return dir;
}

Report sanityOf(const fs::path& dir) {
    Report r;
    checkSanity(loadCapture(dir), realEnv(), r);
    return r;
}

} // namespace

// ---- RPL-05 (a): the read-back follows the capture order, across steps

TEST_CASE("RPL-05 read-back: a write read back by a later step passes") {
    const fs::path dir =
        readBackCapture("rpl05_chain", writeWord("W-01", 300, 7) + readWord("R-02", 300, 7));
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks > 0);
}

TEST_CASE("RPL-05 read-back: a write overwritten by a later write before any read passes") {
    // W-01 is never read: W-02 overwrites the point first and W-02 itself is read back in its own
    // step (the GV-03 -> GV-04 shape).
    const fs::path dir =
        readBackCapture("rpl05_overwritten", writeWord("W-01", 300, 7) + writeWord("W-02", 300, 8) +
                                                 readWord("W-02.2", 300, 8));
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
}

TEST_CASE("RPL-05 read-back: a later read that returns another value still fails") {
    // no read between the write and the later read: the later read is the read-back
    const fs::path dir =
        readBackCapture("rpl05_other", writeWord("W-01", 300, 7) + readWord("R-02", 300, 9));
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(hasFailure(r, "RPL-05", "W-01"), dump(r));
}

TEST_CASE("RPL-05 read-back: a read in a later step that disagrees with the write fails even "
          "after a matching read") {
    // The written value stays pending until the point is written again: every later read of the
    // point is compared with it.
    const fs::path dir = readBackCapture("rpl05_other_later", writeWord("W-01", 300, 7) +
                                                                  readWord("W-01.2", 300, 7) +
                                                                  readWord("R-03", 300, 9));
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(hasFailure(r, "RPL-05", "W-01"), dump(r));
}

TEST_CASE("RPL-05 read-back: a write that no read ever returns still fails") {
    // D301 is read, D300 never: a neighbouring point is no read-back
    const fs::path dir =
        readBackCapture("rpl05_unread", writeWord("W-01", 300, 7) + readWord("R-02", 301, 7));
    const Report r = sanityOf(dir);
    REQUIRE_MESSAGE(hasFailure(r, "RPL-05", "W-01"), dump(r));
    bool named = false;
    for (const Failure& f : r.of("RPL-05")) {
        named = named || f.text().find("has no read-back") != std::string::npos;
    }
    CHECK_MESSAGE(named, dump(r));
}

TEST_CASE("RPL-05 read-back: a write 1, read, write 0, read chain passes (G2-01)") {
    const fs::path dir =
        readBackCapture("rpl05_g2", writeBit("G2-01", 300, 1) + readBit("G2-01.2", 300, 1) +
                                        writeBit("G2-01.3", 300, 0) + readBit("G2-01.4", 300, 0));
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks > 0);
}

TEST_CASE("RPL-05 read-back: write 1, read 0, write 0, read 0 fails on the first write (G2-01)") {
    const fs::path dir = readBackCapture(
        "rpl05_g2_bad", writeBit("G2-01", 300, 1) + readBit("G2-01.2", 300, 0) +
                            writeBit("G2-01.3", 300, 0) + readBit("G2-01.4", 300, 0));
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(hasFailure(r, "RPL-05", "G2-01"), dump(r));
}

// ---- RPL-05 (a) and a link recovery: the values written before it are no longer compared, a write
// not read back before it has no read-back

namespace {

/// Adds "recovery.1: after <step>: ..." to run.meta of @p dir, as the tool writes it.
void addRecovery(const fs::path& dir, const std::string& step) {
    std::string text = slurp(dir / "run.meta");
    if (!text.empty() && text.back() != '\n') {
        text += '\n';
    }
    spit(dir / "run.meta",
         text + "recovery.1: after " + step + ": link lost (state 0); reconnected in 12 ms\n");
}

} // namespace

TEST_CASE("RPL-05 read-back recovery: a write not read back before the recovery still has no "
          "read-back") {
    // the read after the recovery returns the written value, but the memory is not assumed then
    const fs::path dir = readBackCapture("rpl05_recovery_unread",
                                         writeWord("W-01", 300, 7) + readWord("R-02", 300, 7));
    addRecovery(dir, "W-01");
    const Report r = sanityOf(dir);
    REQUIRE_MESSAGE(hasFailure(r, "RPL-05", "W-01"), dump(r));
    bool named = false;
    for (const Failure& f : r.of("RPL-05")) {
        named = named || f.text().find("has no read-back") != std::string::npos;
    }
    CHECK_MESSAGE(named, dump(r));
}

TEST_CASE("RPL-05 read-back recovery: after a recovery on tcp a read of another value is not "
          "reported, on serial it is") {
    const std::string steps = writeWord("W-01", 300, 7) + readWord("W-01.2", 300, 7) +
                              readWord("R-03", 300, 9); // after the recovery
    const fs::path tcp = readBackCapture("rpl05_recovery_tcp", steps);
    addRecovery(tcp, "W-01.2");
    const Report onTcp = sanityOf(tcp);
    CHECK_MESSAGE(onTcp.failures.empty(), dump(onTcp));

    const fs::path serial = readBackCapture("rpl05_recovery_serial", steps);
    setMeta(serial, "transport", "serial");
    addRecovery(serial, "W-01.2");
    const Report onSerial = sanityOf(serial);
    CHECK_MESSAGE(hasFailure(onSerial, "RPL-05", "W-01"), dump(onSerial));
}

// ---- RPL-05 (b): a GV step of a profile whose sum check or block number differs from the vector

namespace {

/// GV-01 (read D100 x3), GV-02 (write M100 x8) and GV-02.2 (read it back) of a 3C format 4
/// capture, with or without the sum bytes. The vectors are V-3C4-01..04.
std::string f4GvSteps(bool sum) {
    const std::string p = "vplc-3c-f4";
    const std::string f = "3C  code: Ascii  format: 4";
    const std::string crlf = " 0D 0A";
    auto s = [sum](const char* bytes) { return sum ? std::string(bytes) : std::string(); };
    return stepPair(p, "GV-01", "  mirrors: V-3C4-01/02", f, "ReadWords", "D100", 3, "api",
                    "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 "
                    "30 30 30 30 30 33" +
                        s(" 30 32") + crlf,
                    "02 46 39 30 30 30 30 46 46 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03" +
                        s(" 39 30") + crlf,
                    "words 1995 1202 1130") +
           stepPair(p, "GV-02", "  mirrors: V-3C4-03/04", f, "WriteBits", "M100", 8, "api",
                    "05 46 39 30 30 30 30 46 46 30 30 31 34 30 31 30 30 30 31 4D 2A 30 30 30 31 "
                    "30 30 30 30 30 38 31 31 30 30 31 31 30 30" +
                        s(" 39 36") + crlf,
                    "06 46 39 30 30 30 30 46 46 30 30 0D 0A", "ok") +
           stepPair(p, "GV-02.2", "", f, "ReadBits", "M100", 8, "api",
                    "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 4D 2A 30 30 30 31 "
                    "30 30 30 30 30 38" +
                        s(" 31 31") + crlf,
                    "02 46 39 30 30 30 30 46 46 30 30 31 31 30 30 31 31 30 30 03" + s(" 42 32") +
                        crlf,
                    "bits 1 1 0 0 1 1 0 0");
}

/// GV-01 (read D100 x3, vector V-3C2-01/02) of a 3C format 2 capture with block number 5A: the
/// block bytes are 35 41 instead of 30 30 and both sums grow by 0x16.
std::string f2Block5aGvSteps() {
    return stepPair("vplc-3c-f4", "GV-01", "  mirrors: V-3C2-01/02", "3C  code: Ascii  format: 2",
                    "ReadWords", "D100", 3, "api",
                    "05 35 41 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 "
                    "30 31 30 30 30 30 30 33 37 38",
                    "02 35 41 46 39 30 30 30 30 46 46 30 30 31 39 39 35 31 32 30 32 31 31 33 30 "
                    "03 30 36",
                    "words 1995 1202 1130");
}

fs::path f4GvCapture(const std::string& name, bool sum) {
    const fs::path dir = scratchCopy("vplc-3c-f4", name);
    if (!sum) {
        setMeta(dir, "frame.sumCheck", "false");
    }
    spit(dir / "steps.vec", f4GvSteps(sum));
    return dir;
}

fs::path f2Block5aGvCapture(const std::string& name) {
    const fs::path dir = scratchCopy("vplc-3c-f4", name);
    setMeta(dir, "frame.format", "Format2");
    setMeta(dir, "frame.blockNo", "90"); // 0x5A
    spit(dir / "steps.vec", f2Block5aGvSteps());
    return dir;
}

/// The hand-built capture must itself be sound: RPL-01 re-encodes every request and RPL-02 parses
/// every response with the profile's config. Both pass today and after the change.
void requireSound(const fs::path& dir) {
    const Capture c = loadCapture(dir);
    Report r;
    checkReencode(c, r);
    checkParse(c, r);
    REQUIRE_MESSAGE(r.failures.empty(), "the hand-built capture is not sound:" << dump(r));
}

} // namespace

TEST_CASE("RPL-05 GV per profile: a serial GV step with the vector's sum check and block compares "
          "bytes (control)") {
    const fs::path dir = f4GvCapture("rpl05_f4_sum", true);
    requireSound(dir);
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks > 0);
}

TEST_CASE("RPL-05 GV per profile: a profile without sum check passes the GV check") {
    // frame.sumCheck false, the sum bytes removed from the GV request and response
    const fs::path dir = f4GvCapture("rpl05_f4_nosum", false);
    requireSound(dir);
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks > 0);
}

TEST_CASE("RPL-05 GV per profile: a profile with block number 5A passes the GV check") {
    // frame.blockNo 90 (0x5A), the block bytes of request and response and their sums changed
    const fs::path dir = f2Block5aGvCapture("rpl05_f2_5a");
    requireSound(dir);
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks > 0);
}

TEST_CASE("RPL-05 GV per profile negative: a changed command byte still fails (no sum check)") {
    const fs::path dir = f4GvCapture("rpl05_f4_nosum_cmd", false);
    editByte(dir / "steps.vec", "CAP-vplc-3c-f4-GV-01", 12, "35"); // command 0401 -> 0501
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(hasFailure(r, "RPL-05", "GV-01"), dump(r));
}

TEST_CASE("RPL-05 GV per profile negative: a changed command byte still fails (block 5A)") {
    const fs::path dir = f2Block5aGvCapture("rpl05_f2_5a_cmd");
    editByte(dir / "steps.vec", "CAP-vplc-3c-f4-GV-01", 14, "35"); // command 0401 -> 0501
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(hasFailure(r, "RPL-05", "GV-01"), dump(r));
}

TEST_CASE("RPL-05 GV per profile negative: a changed device byte still fails (no sum check)") {
    const fs::path dir = f4GvCapture("rpl05_f4_nosum_dev", false);
    editByte(dir / "steps.vec", "CAP-vplc-3c-f4-GV-02", 19, "42"); // device M -> B
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(hasFailure(r, "RPL-05", "GV-02"), dump(r));
}

TEST_CASE("RPL-05 GV per profile negative: a response that no longer parses fails (no sum check)") {
    // The vector answers ok; the captured response of GV-01 has a data character that is not hex.
    const fs::path dir = f4GvCapture("rpl05_f4_nosum_rsp", false);
    editByte(dir / "steps.vec", "CAP-vplc-3c-f4-GV-01-R", 11, "47"); // first data character -> G
    const Report r = sanityOf(dir);
    CHECK_MESSAGE(hasFailure(r, "RPL-05", "GV-01-R"), dump(r));
}

// ---- RPL-02: a step that ran with an override is parsed with that override applied

namespace {

const char* const kReq5A =
    "05 35 41 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 "
    "30 31 37 36";
const char* const kRsp5A = "02 35 41 46 39 30 30 30 30 46 46 30 30 30 30 30 30 03 36 34";
// the bytes of the step G8-Q2-M of q03ude-c24-3c-f2 (block 5B)
const char* const kReq5B =
    "05 35 42 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 "
    "30 31 37 37";
const char* const kRsp5B = "02 35 42 46 39 30 30 30 30 46 46 30 30 30 30 30 30 03 36 35";
// the same answer with D100 = 04D2 (data 30 34 44 32, sum +0x1A): the mock only repeats it when it
// has seeded D100 from the response, which needs the override
const char* const kRsp5BData = "02 35 42 46 39 30 30 30 30 46 46 30 30 30 34 44 32 03 37 46";

/// A 3C format 2 capture with the profile's block number @p blockNo and one step: ReadWords D100 x1
/// that ran with @p overrideText (none when empty).
fs::path f2OverrideCapture(const std::string& name, const char* blockNo, const char* requestHex,
                           const char* responseHex, const std::string& overrideText) {
    const fs::path dir = scratchCopy("vplc-3c-f4", name);
    setMeta(dir, "frame.format", "Format2");
    setMeta(dir, "frame.blockNo", blockNo);
    spit(dir / "steps.vec",
         stepPair("vplc-3c-f4", "G8-Q2-M", "", "3C  code: Ascii  format: 2", "ReadWords", "D100", 1,
                  "mutate", requestHex, responseHex, "record", overrideText));
    return dir;
}

} // namespace

TEST_CASE("RPL-02 override: the bytes of the step are sound for the profile that owns them "
          "(control)") {
    // block 5A with its own echo, block 5B with its own echo: no override anywhere
    for (const char* which : {"a", "b"}) {
        const bool a = std::string(which) == "a";
        const fs::path dir = f2OverrideCapture(std::string("rpl02_ctrl_") + which, a ? "90" : "91",
                                               a ? kReq5A : kReq5B, a ? kRsp5A : kRsp5B, "");
        Report r;
        checkParse(loadCapture(dir), r);
        CHECK_MESSAGE(r.failures.empty(), which << dump(r));
        CHECK(r.checks > 0);
    }
}

TEST_CASE(
    "RPL-02 override: a step with override blockNo=91 whose response echoes block 5B passes") {
    const fs::path dir = f2OverrideCapture("rpl02_override_5b", "90", kReq5B, kRsp5B, "blockNo=91");
    Report r;
    checkParse(loadCapture(dir), r);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks > 0);
    const Report all = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(!hasFailure(all, "RPL-02", "G8-Q2-M"), dump(all));
}

TEST_CASE("RPL-02 override negative: a response that echoes the profile's block 5A instead of "
          "the override's 5B fails") {
    const fs::path dir =
        f2OverrideCapture("rpl02_override_wrong", "90", kReq5B, kRsp5A, "blockNo=91");
    Report r;
    checkParse(loadCapture(dir), r);
    REQUIRE_MESSAGE(hasFailure(r, "RPL-02", "G8-Q2-M"), dump(r));
    CHECK(r.of("RPL-02").front().text().find("vplc-3c-f4") != std::string::npos);
}

TEST_CASE("RPL-02 override negative: an unknown override key is reported as an error") {
    // The bytes are sound for the profile (control above): only the unknown key can be reported.
    const fs::path dir =
        f2OverrideCapture("rpl02_override_unknown", "90", kReq5A, kRsp5A, "bogusKey=1");
    bool reported = false;
    std::string what;
    try {
        const Capture c = loadCapture(dir);
        Report r;
        checkParse(c, r);
        what = dump(r);
        reported = !r.failures.empty();
    } catch (const std::exception& e) {
        what = e.what();
        reported = true;
    }
    CHECK_MESSAGE(reported,
                  "an override key the replay does not know must not be skipped silently");
    CHECK_MESSAGE(what.find("bogusKey") != std::string::npos, what);
}

TEST_CASE("RPL-02 override: a relative stationNo=+1 adds to the profile's station") {
    // 3C format 2, block 00 (the vector's): V-3C2-01/02 with the station field 01 and the sums +1
    const char* const req =
        "05 30 30 46 39 30 31 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 "
        "30 30 30 33 36 33";
    const char* const rsp =
        "02 30 30 46 39 30 31 30 30 46 46 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 46 31";
    const fs::path ctrl = scratchCopy("vplc-3c-f4", "rpl02_station_ctrl");
    setMeta(ctrl, "frame.format", "Format2");
    setMeta(ctrl, "frame.stationNo", "1"); // the station of the bytes, as the profile
    spit(ctrl / "steps.vec", stepPair("vplc-3c-f4", "G8-S", "", "3C  code: Ascii  format: 2",
                                      "ReadWords", "D100", 3, "mutate", req, rsp, "record"));
    Report rc;
    checkParse(loadCapture(ctrl), rc);
    REQUIRE_MESSAGE(rc.failures.empty(), "control:" << dump(rc));

    const fs::path dir = scratchCopy("vplc-3c-f4", "rpl02_station");
    setMeta(dir, "frame.format", "Format2"); // profile station 0, the step ran with 0 + 1
    spit(dir / "steps.vec",
         stepPair("vplc-3c-f4", "G8-S", "", "3C  code: Ascii  format: 2", "ReadWords", "D100", 3,
                  "mutate", req, rsp, "record", "stationNo=+1"));
    Report r;
    checkParse(loadCapture(dir), r);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
}

TEST_CASE("RPL-03 override: the mock answers a step that ran with override blockNo=91 with the "
          "captured bytes") {
    const fs::path dir =
        f2OverrideCapture("rpl03_override_5b", "90", kReq5B, kRsp5BData, "blockNo=91");
    Report r;
    checkMock(loadCapture(dir), r);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks > 0);
}

// ---- RPL-04: a poll round's duration may differ from the recorded one by up to 5 ms

namespace {

/// The fixture's round 2 lasted 1 ms; its recorded duration is raised by @p deltaMs.
Report durationOff(const std::string& name, int deltaMs) {
    const fs::path dir = scratchCopy("vplc-3e-bin", name);
    const std::string cycle = recordWith(dir / "session.vec", "round: 2  duration_ms");
    bumpByte(dir / "session.vec", cycle, 13, deltaMs);
    Report r;
    checkSession(loadCapture(dir), r);
    return r;
}

} // namespace

TEST_CASE("RPL-04 tolerance: a cycle duration 4 ms off passes") {
    const Report r = durationOff("rpl04_dur4", 4);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
}

TEST_CASE("RPL-04 tolerance: a cycle duration 5 ms off passes, 6 ms off fails") {
    const Report five = durationOff("rpl04_dur5", 5);
    CHECK_MESSAGE(five.failures.empty(), "5 ms:" << dump(five));
    const Report six = durationOff("rpl04_dur6", 6);
    REQUIRE_MESSAGE(hasFailure(six, "RPL-04", "E-12"), "6 ms:" << dump(six));
    CHECK_MESSAGE(failuresOf(six, "RPL-04").find("duration") != std::string::npos, dump(six));
}

TEST_CASE("RPL-04 tolerance: a cycle duration 10 ms off still fails") {
    const Report r = durationOff("rpl04_dur10", 10);
    REQUIRE_MESSAGE(hasFailure(r, "RPL-04", "E-12"), dump(r));
    CHECK_MESSAGE(failuresOf(r, "RPL-04").find("duration") != std::string::npos, dump(r));
}

// ---- RPL-03 target: run.meta `plc`, `supports`, `special_word` / `special_bit` -> the mock
// -------
//
// The replay builds its MockPlc from run.meta: `plc` chooses the MockTarget (Q03UDECPU -> QCpu,
// FX5U-... -> Fx5, FX3U-... -> Fx3, anything else or no key -> Generic); for Generic and FX3U every
// device type outside `supports` (when the key is there) is switched off with setTypeSupported
// (Q03UDECPU and FX5U-... refuse the types their captures show refused), and on an FX3 the
// special range named by `special_word` / `special_bit` (D8000 / M8000, beyond device_end) exists
// up to number 8511. A recovered link builds a fresh mock the same way. The captures below are
// small folders written in the build tree; every request and response is a record of a hardware
// capture (ids CAP-q03ude-eth-3e-bin-..., CAP-fx5u-eth-3e-bin-..., CAP-fx3-eth-1e-bin-...), the
// bytes the PLC sent.

namespace {

struct TargetStep {
    const char* id;
    const char* op;
    const char* device;
    int count;
    const char* outcome; // "ok" or "plcError <code>"
    const char* request;
    const char* response;
};

struct TargetMeta {
    const char* plc;      // nullptr: the key is absent
    const char* supports; // nullptr: the key is absent
    const char* deviceEnd;
    const char* specialWord; // nullptr: the key is absent
    const char* specialBit;
    bool oneE; // 1E Binary (FX3U-ENET-ADP); else 3E Binary
};

std::string targetRunMeta(const std::string& name, const TargetMeta& m) {
    std::ostringstream t;
    t << "format: hil-capture-1\n"
      << "profile: " << name << "\n";
    if (m.plc != nullptr) {
        t << "plc: " << m.plc << "\n";
    }
    t << "transport: tcp\n"
      << "device_end: " << m.deviceEnd << "\n";
    if (m.supports != nullptr) {
        t << "supports: " << m.supports << "\n";
    }
    if (m.specialBit != nullptr) {
        t << "special_bit: " << m.specialBit << "\n";
    }
    if (m.specialWord != nullptr) {
        t << "special_word: " << m.specialWord << "\n";
    }
    t << "frame.aSeriesTarget: false\n"
      << "frame.blockNo: 0\n"
      << "frame.checkBlockNo: true\n"
      << "frame.checkRoute: false\n"
      << "frame.code: Binary\n"
      << "frame.commandSet: ACPU\n"
      << "frame.e1AliasLS: false\n"
      << "frame.f3ShortResponseHasSum: false\n"
      << "frame.format: Format1\n"
      << "frame.frame: " << (m.oneE ? "1E" : "3E") << "\n"
      << "frame.highPerformanceQcpu: false\n"
      << "frame.io: 1023\n"
      << "frame.messageWait: 0\n"
      << "frame.monitoringTimer: " << (m.oneE ? 10 : 16) << "\n"
      << "frame.network: 0\n"
      << "frame.pc: 255\n"
      << "frame.readRetries: 0\n"
      << "frame.selfStation: 0\n"
      << "frame.sendEotOnError: true\n"
      << "frame.serialStart: 0\n"
      << "frame.series: QL\n"
      << "frame.splitWrites: false\n"
      << "frame.station: 0\n"
      << "frame.stationNo: 0\n"
      << "frame.sumCheck: true\n"
      << "frame.targetFamily: " << (m.oneE ? "A" : "IqR_Q_L") << "\n"
      << "frame.timeoutMs: 0\n"
      << "frame.xyAsciiDigits: Hex\n"
      << "frame.xyNotation: " << (m.oneE ? "Octal" : "Hex") << "\n"
      << "session.adHocArenaBytes: 65536\n"
      << "session.adHocCapacity: 64\n"
      << "session.bitsAsWords: true\n"
      << "session.cycleIntervalMs: 100\n"
      << "session.cycleMode: FixedRate\n"
      << "session.firstResponseTimeoutMs: 0\n"
      << "session.heartbeat.device: M2000\n"
      << "session.heartbeat.enabled: false\n"
      << "session.maxAdHocBurst: 4\n"
      << "session.maxConsecutiveLinkErrors: 3\n"
      << "session.maxGap: auto\n"
      << "session.serialFlushMs: 50\n"
      << "session.serialInterCharMs: 100\n";
    return t.str();
}

// A capture folder under the build tree: run.meta, steps.vec and an empty session.vec.
// `recoveryAfter` names the steps after which the link was re-established.
fs::path targetCapture(const std::string& name, const TargetMeta& m,
                       const std::vector<TargetStep>& steps,
                       const std::vector<std::string>& recoveryAfter = {}) {
    const fs::path dir = scratchDir() / "rpl03_target" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string meta = targetRunMeta(name, m);
    int n = 0;
    for (const std::string& after : recoveryAfter) {
        meta += "recovery." + std::to_string(++n) + ": after " + after +
                ": PeerClosed (Disconnected); reconnected in 2 ms\n";
    }
    spit(dir / "run.meta", meta);
    std::string text;
    for (const TargetStep& s : steps) {
        const std::string id = std::string("CAP-") + name + "-" + s.id;
        const bool ok = std::string(s.outcome) == "ok";
        text += "# id: " + id + "\n";
        text += std::string("# source: plc  profile: ") + name + "  step: " + s.id + "\n";
        text += std::string("# frame: ") + (m.oneE ? "1E" : "3E") + "  code: Binary  op: " + s.op +
                "  device: " + s.device + "  count: " + std::to_string(s.count) + "  via: api\n";
        text += "# kind: request\n";
        text += std::string(s.request) + "\n\n";
        text += "# id: " + id + "-R\n";
        text += "# kind: response  of: " + id + "\n";
        text += std::string("# outcome: ") + s.outcome + "  expect: " + (ok ? "ok" : "plcError") +
                "  verdict: passed\n";
        text += std::string(s.response) + "\n\n";
    }
    spit(dir / "steps.vec", text);
    spit(dir / "session.vec", "");
    return dir;
}

// The same record again under another step id (a vec id must be unique in a steps.vec).
TargetStep again(TargetStep s, const char* id) {
    s.id = id;
    return s;
}

Report mockCheckOf(const fs::path& dir) {
    Report r;
    checkMock(loadCapture(dir), r);
    return r;
}

// The captured records used below.
const TargetStep kQ3G502{"G5-02",
                         "ReadWords",
                         "D12288",
                         1,
                         "plcError 4031",
                         "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 30 00 A8 01 00",
                         "D0 00 00 FF FF 03 00 0B 00 31 40 00 FF FF 03 00 01 04 00 00"};
const TargetStep kQ3G402R{"G4-02-R",
                          "ReadWords",
                          "R0",
                          1,
                          "plcError 4031",
                          "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 00 00 AF 01 00",
                          "D0 00 00 FF FF 03 00 0B 00 31 40 00 FF FF 03 00 01 04 00 00"};
const TargetStep kF5G502{"G5-02",
                         "ReadWords",
                         "D8000",
                         1,
                         "plcError C056",
                         "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 40 1F 00 A8 01 00",
                         "D0 00 00 FF FF 03 00 0B 00 56 C0 00 FF FF 03 00 01 04 00 00"};
const TargetStep kF5G402V{"G4-02-V",
                          "ReadBits",
                          "V0",
                          1,
                          "plcError C05C",
                          "50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 00 00 00 94 01 00",
                          "D0 00 00 FF FF 03 00 0B 00 5C C0 00 FF FF 03 00 01 04 01 00"};
const TargetStep kF5G402DX{"G4-02-DX",
                           "ReadBits",
                           "DX0",
                           1,
                           "plcError C05C",
                           "50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 00 00 00 A2 01 00",
                           "D0 00 00 FF FF 03 00 0B 00 5C C0 00 FF FF 03 00 01 04 01 00"};
const TargetStep kF3G504{"G5-04", "ReadBits",      "M7680",
                         1,       "plcError 0058", "00 FF 0A 00 00 1E 00 00 20 4D 01 00",
                         "80 58"};
const TargetStep kF3G402F{"G4-02-F", "ReadBits",      "F0",
                          1,         "plcError 0056", "00 FF 0A 00 00 00 00 00 20 46 01 00",
                          "80 56"};
const TargetStep kF3G002{
    "G0-02", "ReadWords", "D8010", 1, "ok", "01 FF 0A 00 4A 1F 00 00 20 44 01 00", "81 00 05 00"};
const TargetStep kF3G109{"G1-09",
                         "ReadWords",
                         "D8000",
                         4,
                         "ok",
                         "01 FF 0A 00 40 1F 00 00 20 44 04 00",
                         "81 00 C8 00 77 66 08 00 10 00"};
const TargetStep kF3G210{"G2-10",
                         "ReadBits",
                         "M8000",
                         16,
                         "ok",
                         "00 FF 0A 00 40 1F 00 00 20 4D 10 00",
                         "80 00 10 01 00 00 00 00 10 00"};
const TargetStep kF3G304{
    "G3-04", "ReadWords", "M8000", 1, "ok", "01 FF 0A 00 40 1F 00 00 20 4D 01 00", "81 00 09 08"};
const TargetStep kF3G502{
    "G5-02", "ReadWords", "D8000", 1, "ok", "01 FF 0A 00 40 1F 00 00 20 44 01 00", "81 00 C8 00"};

const TargetMeta kQ3Meta{
    "Q03UDECPU", "D W M L F B X Y TS TC TN CS CC CN SM SD", "M=8191 D=12287 W=1FFF", "SD0", "SM0",
    false};
const TargetMeta kF5Meta{"FX5U-32M",
                         "D W R M L F B X Y TS TC TN CS CC CN SM SD",
                         "M=7679 D=7999 R=32767 V=2047",
                         "SD0",
                         "SM0",
                         false};
const TargetMeta kF3Meta{"FX3U-32M",
                         "D R M S X Y TS TN CS CN",
                         "X=377 Y=377 M=7679 D=7999 TS=511 TN=511 CS=255 CN=255 S=4095 R=32767",
                         "D8000",
                         "M8000",
                         true};

} // namespace

TEST_CASE("RPL-03 target: plc Q03UDECPU replays with the QCpu codes (4031 beyond device_end and "
          "for a type outside supports)") {
    const fs::path dir = targetCapture("t096_q3", kQ3Meta, {kQ3G502, kQ3G402R});
    const Report r = mockCheckOf(dir);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks == 2);
}

TEST_CASE("RPL-03 target: plc FX5U-32M replays with the Fx5 codes (C056 beyond device_end, C05C "
          "for V and DX outside supports)") {
    const fs::path dir = targetCapture("t096_f5", kF5Meta, {kF5G502, kF5G402V, kF5G402DX});
    const Report r = mockCheckOf(dir);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks == 3);
}

TEST_CASE("RPL-03 target: plc FX3U-32M replays with the Fx3 codes and the special range "
          "D8000.. / M8000.. of special_word and special_bit") {
    const fs::path dir = targetCapture(
        "t096_f3", kF3Meta, {kF3G002, kF3G109, kF3G210, kF3G304, kF3G502, kF3G504, kF3G402F});
    const Report r = mockCheckOf(dir);
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks == 7);
}

TEST_CASE("RPL-03 target: the special range is the FX3's; another plc with the same keys has "
          "none, and its D8000 is out of range") {
    // Generic (plc not an FX3U): the 3E Binary request for D8000 is C051 with device_end D=7999.
    const TargetStep d8000Generic{"G5-02",
                                  "ReadWords",
                                  "D8000",
                                  1,
                                  "plcError C051",
                                  "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 40 1F 00 A8 01 00",
                                  "D0 00 00 FF FF 03 00 0B 00 51 C0 00 FF FF 03 00 01 04 00 00"};
    TargetMeta meta = kF5Meta;
    meta.plc = "virtual_plc";
    meta.supports = nullptr;
    meta.specialWord = "D8000";
    meta.specialBit = "M8000";
    const Report r = mockCheckOf(targetCapture("t096_nospecial", meta, {d8000Generic}));
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks == 1);
}

TEST_CASE("RPL-03 target: a missing or unknown plc is Generic (C051 beyond device_end, C059 for a "
          "type outside supports)") {
    const TargetStep rangeGeneric{"G5-02",
                                  "ReadWords",
                                  "D12288",
                                  1,
                                  "plcError C051",
                                  "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 30 00 A8 01 00",
                                  "D0 00 00 FF FF 03 00 0B 00 51 C0 00 FF FF 03 00 01 04 00 00"};
    const TargetStep typeGeneric{"G4-02-R",
                                 "ReadWords",
                                 "R0",
                                 1,
                                 "plcError C059",
                                 "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 00 00 AF 01 00",
                                 "D0 00 00 FF FF 03 00 0B 00 59 C0 00 FF FF 03 00 01 04 00 00"};
    const char* const unknown[] = {nullptr, "virtual_plc", "virtual_plc (FX numbering)", "A2USCPU",
                                   "MockPlc"};
    int n = 0;
    for (const char* plc : unknown) {
        TargetMeta meta = kQ3Meta;
        meta.plc = plc;
        const std::string label = plc == nullptr ? "(none)" : plc;
        const Report r = mockCheckOf(
            targetCapture("t096_generic" + std::to_string(++n), meta, {rangeGeneric, typeGeneric}));
        CHECK_MESSAGE(r.failures.empty(), "plc " << label << dump(r));
        CHECK_MESSAGE(r.checks == 2, "plc " << label);
    }
}

TEST_CASE("RPL-03 target negative: the captured Q03UDE codes fail under a plc that is not a "
          "Q03UDECPU") {
    TargetMeta meta = kQ3Meta;
    meta.plc = "virtual_plc";
    const Report r = mockCheckOf(targetCapture("t096_q3_as_generic", meta, {kQ3G502, kQ3G402R}));
    CHECK_MESSAGE(hasFailure(r, "RPL-03", "G5-02"), dump(r));
    CHECK_MESSAGE(hasFailure(r, "RPL-03", "G4-02-R"), dump(r));
}

TEST_CASE("RPL-03 target: supports applies to Generic and FX3U, and no supports key means every "
          "type is supported") {
    // The R0 read of the Q03UDE capture, answered with data (R is in supports, nothing is switched
    // off): the mock reads 0 from unwritten memory.
    const TargetStep readsR{"G1-01",
                            "ReadWords",
                            "R0",
                            1,
                            "ok",
                            "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 00 00 AF 01 00",
                            "D0 00 00 FF FF 03 00 04 00 00 00 00 00"};
    TargetMeta noSupports = kQ3Meta;
    noSupports.plc = "virtual_plc";
    noSupports.supports = nullptr;
    const Report all = mockCheckOf(targetCapture("t096_nosupports", noSupports, {readsR}));
    CHECK_MESSAGE(all.failures.empty(), dump(all));

    TargetMeta withR = kQ3Meta;
    withR.plc = "virtual_plc";
    withR.supports = "D W R M";
    const Report listed = mockCheckOf(targetCapture("t096_supportsr", withR, {readsR}));
    CHECK_MESSAGE(listed.failures.empty(), dump(listed));

    // R outside supports and the plc not known: the Generic unsupported-type code.
    const TargetStep noR{"G1-01",
                         "ReadWords",
                         "R0",
                         1,
                         "plcError C059",
                         "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 00 00 AF 01 00",
                         "D0 00 00 FF FF 03 00 0B 00 59 C0 00 FF FF 03 00 01 04 00 00"};
    TargetMeta withoutR = kQ3Meta;
    withoutR.plc = "virtual_plc";
    withoutR.supports = "D W M";
    const Report off = mockCheckOf(targetCapture("t096_supportsnor", withoutR, {noR}));
    CHECK_MESSAGE(off.failures.empty(), dump(off));
}

TEST_CASE("RPL-03 target: a link recovery rebuilds the mock with the same target, supports and "
          "special range") {
    // FX3U: after the recovery the mock is new (memory lost), yet it still answers 58H, 56H and the
    // special range D8010.
    const fs::path f3 = targetCapture("t096_f3_recovery", kF3Meta,
                                      {kF3G109, kF3G504, kF3G402F, kF3G002}, {"G1-09"});
    const Report r3 = mockCheckOf(f3);
    CHECK_MESSAGE(r3.failures.empty(), dump(r3));
    CHECK(r3.checks == 4);

    // FX5U: C056 and C05C after the recovery.
    const fs::path f5 =
        targetCapture("t096_f5_recovery", kF5Meta,
                      {kF5G502, kF5G402V, kF5G402DX, again(kF5G502, "G5-02.2")}, {"G5-02"});
    const Report r5 = mockCheckOf(f5);
    CHECK_MESSAGE(r5.failures.empty(), dump(r5));
    CHECK(r5.checks == 4);

    // Q03UDE: 4031 after the recovery.
    const fs::path q3 = targetCapture("t096_q3_recovery", kQ3Meta,
                                      {kQ3G502, kQ3G402R, again(kQ3G502, "G5-02.2")}, {"G4-02-R"});
    const Report rq = mockCheckOf(q3);
    CHECK_MESSAGE(rq.failures.empty(), dump(rq));
    CHECK(rq.checks == 3);
}

// ---- RPL-03 target: Q03UDECPU and FX5U refuse only the captured types ----------------------

namespace {

// Captured G4-02 steps of types outside the profile's `supports` that the PLC answered with data
// (CAP-q03ude-eth-3e-bin-G4-02-V-R and CAP-fx5u-eth-3e-bin-G4-02-Z-R).
const TargetStep kQ3G402VOk{"G4-02-V",
                            "ReadBits",
                            "V0",
                            1,
                            "ok",
                            "50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 00 00 00 94 01 00",
                            "D0 00 00 FF FF 03 00 03 00 00 00 00"};
const TargetStep kF5G402ZOk{"G4-02-Z",
                            "ReadWords",
                            "Z0",
                            1,
                            "ok",
                            "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 00 00 CC 01 00",
                            "D0 00 00 FF FF 03 00 04 00 00 00 00 00"};

} // namespace

TEST_CASE("RPL-03 target: a Q03UDECPU keeps a type outside supports that its capture did not "
          "refuse (V) executed, and refuses R") {
    const Report r = mockCheckOf(
        targetCapture("t096_q3_refused_table", kQ3Meta, {kQ3G402VOk, kQ3G402R, kQ3G502}));
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks == 3);
}

TEST_CASE("RPL-03 target: an FX5U keeps a type outside supports that its capture did not refuse "
          "(Z) executed, and refuses V") {
    const Report r = mockCheckOf(
        targetCapture("t096_f5_refused_table", kF5Meta, {kF5G402ZOk, kF5G402V, kF5G502}));
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks == 3);
}

TEST_CASE("RPL-03 target: an empty supports value disables nothing") {
    // The R0 read answered with data; the capture lists no type, as a workbench export does.
    const TargetStep readsR{"G1-01",
                            "ReadWords",
                            "R0",
                            1,
                            "ok",
                            "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 00 00 AF 01 00",
                            "D0 00 00 FF FF 03 00 04 00 00 00 00 00"};
    TargetMeta meta = kQ3Meta;
    meta.plc = "virtual_plc";
    meta.supports = "";
    const Report r = mockCheckOf(targetCapture("t096_emptysupports", meta, {readsR}));
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks == 1);
}

// The replay pins the upper end of the FX3 special range (D8000-D8511 and M8000-M8511 per the FX3
// manual). The step records are synthetic, shaped like the captured 1E Binary records of
// fx3-eth-1e-bin (G1-09 and G2-10 for the frames, G5-04 for 58H).
TEST_CASE("RPL-03 target: the FX3 special range of the replay ends with number 8511") {
    const TargetStep d8511{
        "G9-01",      "ReadWords", "D8511", 1, "ok", "01 FF 0A 00 3F 21 00 00 20 44 01 00",
        "81 00 00 00"};
    const TargetStep d8512{"G9-02", "ReadWords",     "D8512",
                           1,       "plcError 0058", "01 FF 0A 00 40 21 00 00 20 44 01 00",
                           "81 58"};
    const TargetStep m8511{
        "G9-03", "ReadBits", "M8511", 1, "ok", "00 FF 0A 00 3F 21 00 00 20 4D 01 00", "80 00 00"};
    const TargetStep m8512{"G9-04", "ReadBits",      "M8512",
                           1,       "plcError 0058", "00 FF 0A 00 40 21 00 00 20 4D 01 00",
                           "80 58"};
    const Report r =
        mockCheckOf(targetCapture("t096_f3_special_end", kF3Meta, {d8511, d8512, m8511, m8512}));
    CHECK_MESSAGE(r.failures.empty(), dump(r));
    CHECK(r.checks == 4);
}

// ---- RPL-02: the answer to a lone EOT (F-017) ----------------------------------------------------
//
// The tool used to record the EOT exchange of a `recover: eot` step as outcome `ok` whatever the
// PLC answered. The five q03ude-c24-3c-* captures hold `ok` for the NAK (CAP-...-G7-01+2-R). A
// recorded `ok` on a lone EOT request (04, 04 0D 0A) whose bytes parse to a PLC error is read as
// that plcError; nothing wider.

namespace {

// 04 0D 0A answered with the f4 NAK of CAP-q03ude-c24-3c-f4-G7-01+2-R (error 7F23).
const char* const kNakF4 = "15 46 39 30 30 30 30 46 46 30 30 37 46 32 33 0D 0A";

/// One request record and its response record, shaped as the tool writes them.
std::string pairBlock(const std::string& step, const std::string& requestHex,
                      const std::string& outcome, const std::string& responseHex) {
    const std::string id = "CAP-vplc-3c-f4-" + step;
    return "# id: " + id + "\n# source: plc  profile: vplc-3c-f4  step: " + step +
           "\n# frame: 3C  code: Ascii  format: 4  op: ReadWords  device: D100  count: 1  via: "
           "mutate\n# kind: request\n" +
           requestHex + "\n\n# id: " + id + "-R\n# kind: response  of: " + id +
           "\n# outcome: " + outcome + "  verdict: passed\n" + responseHex + "\n";
}

fs::path scratchWithBlock(const std::string& name, const std::string& block) {
    const fs::path dir = scratchCopy("vplc-3c-f4", name);
    spit(dir / "steps.vec", slurp(dir / "steps.vec") + block);
    return dir;
}

} // namespace

TEST_CASE("RPL-02 a recorded ok on a lone EOT whose bytes are a PLC error is read as that error") {
    const fs::path dir = scratchWithBlock("rpl02_eot", pairBlock("G7-01+2", "04 0D 0A", "ok", kNakF4));
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(!hasFailure(r, "RPL-02", "G7-01+2"), dump(r));
}

TEST_CASE("RPL-02 the one-byte EOT of a format that sends 04 alone is a lone EOT too") {
    const fs::path dir = scratchWithBlock("rpl02_eot1", pairBlock("G7-01+2", "04", "ok", kNakF4));
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(!hasFailure(r, "RPL-02", "G7-01+2"), dump(r));
}

TEST_CASE("RPL-02 negative: a recorded ok on a lone EOT whose answer does not parse still fails") {
    // The first byte of the NAK is damaged (15 -> 07): neither ok nor a PLC error.
    const fs::path dir = scratchWithBlock("rpl02_eot_damaged",
                                          pairBlock("G7-01+2", "04 0D 0A", "ok", kNakF4));
    editByte(dir / "steps.vec", "CAP-vplc-3c-f4-G7-01+2-R", 0, "07");
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-02", "G7-01+2"), dump(r));
}

TEST_CASE("RPL-02 negative: a recorded ok on a lone EOT whose answer fails to parse as a PLC error "
          "still fails") {
    // A NAK frame whose error code is not hex digits: the parser fails with a protocol error, not
    // a PLC error, so the rule does not apply.
    const fs::path dir = scratchWithBlock(
        "rpl02_eot_badcode",
        pairBlock("G7-01+2", "04 0D 0A", "ok", "15 46 39 30 30 30 30 46 46 30 30 5A 5A 5A 5A 0D 0A"));
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-02", "G7-01+2"), dump(r));
}

TEST_CASE("RPL-02 negative: a recorded ok on a request that is not an EOT but parses to a PLC error "
          "still fails") {
    // The read of E-01 (05 F9 ...), recorded ok, answered with the NAK.
    const fs::path dir = scratchWithBlock(
        "rpl02_not_eot",
        pairBlock("G7-09",
                  "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 "
                  "30 30 30 30 31 30 30 0D 0A",
                  "ok", kNakF4));
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-02", "G7-09"), dump(r));
}

TEST_CASE("RPL-02 negative: a frame that merely starts with an EOT byte is not a lone EOT") {
    // 04 followed by a read request: not the exchange the rule is about.
    const fs::path dir = scratchWithBlock(
        "rpl02_eot_prefix",
        pairBlock("G7-08",
                  "04 05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 "
                  "30 30 30 30 30 31 30 30 0D 0A",
                  "ok", kNakF4));
    const Report r = checkAll(loadCapture(dir), realEnv());
    CHECK_MESSAGE(hasFailure(r, "RPL-02", "G7-08"), dump(r));
}
