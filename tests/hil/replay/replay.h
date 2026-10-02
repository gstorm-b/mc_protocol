// tests/hil/replay/replay.h (SPEC-hil-capture.md "Replay tests", RPL-01..06): the checks that
// replay a capture folder (run.meta, steps.vec, session.vec, divergences.txt) against the encoder,
// the parser, the mock and the Session. Standard C++ only: links mc::core and mc::mock, never Qt, a
// network or a serial port. Test code: may allocate, may throw.
//
// A capture folder is read with the shared loader tests/common/vectors.h. The formats are the ones
// documented in the header comment of tools/hil_capture/capture_writer.h.
//
// Findings are returned, never printed: every Failure names the check, the profile and the step.
// A failure whose step is listed in the profile's divergences.txt is reported as a known
// divergence instead and does not fail the run (spec decision H3).
#pragma once

#include "common/vectors.h"

#include "mc/core/frame_config.h"
#include "mc/core/session.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mc::replay {

/// One finding of one check.
struct Failure {
    std::string check;   ///< "RPL-01" ... "RPL-06".
    std::string profile; ///< Profile id (the capture folder's `profile:` in run.meta).
    std::string step;    ///< Step or record id, e.g. "G1-04+2"; empty for a whole-folder finding.
    std::string message; ///< What differs.

    /// "RPL-01 <profile> step <step>: <message>", the text a failing test prints.
    std::string text() const;
};

/// Everything the checks of one capture folder found.
struct Report {
    std::vector<Failure> failures;         ///< Real failures.
    std::vector<Failure> knownDivergences; ///< Failures of steps tagged in divergences.txt.
    std::vector<std::string> notes;        ///< Things skipped on purpose, one line each.
    int checks{0};                         ///< Individual comparisons made (all checks together).

    /// True when no real failure was found.
    bool ok() const { return failures.empty(); }
    /// The failures of one check.
    std::vector<Failure> of(const std::string& check) const;
};

/// One request/response exchange of steps.vec.
struct Record {
    std::string id;                ///< Record id "G1-04", "G1-04.2", "G1-04+2".
    std::string step;              ///< The plan step: the id without ".N" / "+N".
    std::string via;               ///< "api", "mutate" or "raw".
    std::string op;                ///< "ReadWords", "WriteBits", ..., "Raw".
    std::string device;            ///< Head device text; empty for a raw frame.
    int count{0};                  ///< Points or words.
    std::string mirrors;           ///< Appendix A vectors this mirrors ("V-3E-B-05/06"), or empty.
    std::string frame;             ///< "3E", "1E", "3C", "1C".
    std::string code;              ///< "Binary" or "Ascii".
    int format{0};                 ///< Serial format, 0 for Ethernet.
    std::string overrideText;      ///< `override:` key: the step ran with another FrameConfig.
    std::vector<uint8_t> request;  ///< Bytes written.
    std::vector<uint8_t> response; ///< Bytes received; empty when none came back.
    bool hasResponse{false};       ///< A response record exists.
    bool partial{false};           ///< The response record is `response-partial`.
    std::string outcome;           ///< The recorded outcome text.
    std::string expect;            ///< The recorded expectation text.
    std::string verdict; ///< passed, failed, diverged or unsupported; empty in an older capture.
};

/// One capture folder, loaded.
struct Capture {
    std::filesystem::path dir;                                   ///< The folder.
    std::string profile;                                         ///< run.meta `profile`.
    std::map<std::string, std::string> meta;                     ///< Every run.meta key.
    std::vector<std::pair<std::string, std::string>> recoveries; ///< recovery.<n>: id after, text.
    FrameConfig frame;                                      ///< Rebuilt from the `frame.*` keys.
    SessionConfig session;                                  ///< Rebuilt from the `session.*` keys.
    std::vector<std::pair<DeviceType, uint32_t>> deviceEnd; ///< From `device_end`.
    std::string transport;                                  ///< "tcp" or "serial".
    std::vector<Record> records;                            ///< steps.vec, in file order.
    bool hasSessionFile{false};                ///< session.vec exists (the tool always writes it).
    std::vector<mc::test::Vector> sessionVecs; ///< session.vec records, in file order.
    std::vector<std::pair<std::string, std::string>> divergences; ///< Step id, FINDINGS id.
};

/// What the checks need from outside the capture folder.
struct Env {
    std::filesystem::path vectorsDir;   ///< tests/vectors (the Appendix A golden vectors, RPL-05).
    std::filesystem::path findingsFile; ///< docs/hil/FINDINGS.md (RPL-06).
};

/// Loads one capture folder. Throws std::runtime_error ("<file>: <reason>") on a missing or
/// malformed file or key.
Capture loadCapture(const std::filesystem::path& dir);

/// The sub-folders of @p root that look like a capture (hold any file the tool writes: run.meta,
/// steps.vec, session.vec, bench.csv, divergences.txt), sorted; empty when @p root does not exist
/// or holds none. A folder without run.meta is returned too: loading it fails, which is how a
/// damaged capture is reported.
std::vector<std::filesystem::path> captureFolders(const std::filesystem::path& root);

/// RPL-01: re-encode every api request from its metadata with the run.meta FrameConfig.
void checkReencode(const Capture& c, Report& r);
/// RPL-02: every response parses to the recorded outcome and values.
void checkParse(const Capture& c, Report& r);
/// RPL-03: mock conformance (steps replayed in order into a MockPlc).
void checkMock(const Capture& c, Report& r);
/// RPL-04: the poll transcripts replayed through a Session on a fake clock. A missing session.vec,
/// or a poll step that run.meta lists in its `polls` key and session.vec has no transcript for, is
/// a failure.
void checkSession(const Capture& c, Report& r);
/// RPL-05: write read-backs matched; GV steps equal their Appendix A vectors.
void checkSanity(const Capture& c, const Env& env, Report& r);
/// RPL-06: every divergences.txt entry has a FINDINGS.md entry.
void checkDivergences(const Capture& c, const Env& env, Report& r);

/// Runs RPL-01..06 on @p c and sorts the failures into failures / knownDivergences.
Report checkAll(const Capture& c, const Env& env);

} // namespace mc::replay
