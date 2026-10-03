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
    const fs::path dir = scratchCopy("vplc-3e-bin", "rpl05_noreadback");
    editAfter(dir / "steps.vec", "CAP-vplc-3e-bin-GV-01.2", "step: GV-01.2", "step: ZZ-01.2");
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
    editByte(dir / "steps.vec", "CAP-vplc-3e-bin-E-04", 15, "65"); // would fail RPL-01 and RPL-03
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

TEST_CASE("RPL-04 a cycle duration may differ by two milliseconds, not by ten") {
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
