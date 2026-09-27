// tests/common/vectors.h — loader for the `.vec` golden-vector text format described by
// SPEC-core-protocol.md, "Golden vector files": `#`-prefixed `key: value` metadata lines
// (several keys allowed per line), exactly one hex line per vector, and an optional ASCII text
// line that spells non-printable control bytes as `<STX>`-style names. Every metadata key is
// exposed as plain string data (nothing here knows what "op" or "device" mean) so this loader
// stays generic across its three consumers: core-protocol's own codec tests (T-014 onward),
// tests/mock (server direction, same vectors read in reverse) and tests/replay, once those
// exist (SPEC-core-protocol.md, SPEC-mock-plc.md "Changes required in other specs").
//
// Test code only: may allocate, may throw. Owned by core-protocol (T-013); do not move without
// updating every consumer above.
#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mc::test {

/// One `key: value` metadata field, in the order it appeared in its `.vec` file.
struct VecField {
    std::string key;
    std::string value;
};

/// One `# id: ...` record loaded from a `.vec` file: its metadata, the decoded hex line, and
/// (for serial/ASCII frames) the optional control-name text line.
struct Vector {
    std::string id;                ///< From the mandatory `id` field.
    std::vector<VecField> fields;  ///< Every metadata field of this record, in file order.
    std::vector<uint8_t> bytes;    ///< Decoded hex line.
    std::string text;              ///< Decoded ASCII text line, verbatim; empty when absent.
    bool hasText = false;          ///< Whether an ASCII text line was present for this record.
    std::vector<std::string> tags; ///< From `tags:` fields (space/comma separated), e.g. "v1.1".
    std::string file;              ///< Source file, as given to the loader.
    int line = 0;                  ///< Line number of this record's first metadata line.

    /// First value stored under `key`, or "" if the field is absent.
    std::string field(std::string_view key) const;
    /// Whether `tags` contains exactly `tag`.
    bool hasTag(std::string_view tag) const;
};

/// Thrown by every function below on malformed `.vec` input. what() is always
/// "<file>:<line>: <reason>", per the module spec's "fails with the file name and line number".
class VecFormatError : public std::runtime_error {
public:
    explicit VecFormatError(std::string message) : std::runtime_error(std::move(message)) {}
};

/// Parses `.vec` text already in memory. `fileForMessages` labels every Vector::file and every
/// error message; it need not exist on disk (tests use this to exercise malformed input without
/// writing fixture files). Throws VecFormatError on malformed input.
std::vector<Vector> parseVectors(std::string_view text, std::string_view fileForMessages);

/// Reads one `.vec` file from disk and parses it (see parseVectors). Throws VecFormatError on
/// malformed input, or std::runtime_error if `file` cannot be opened.
std::vector<Vector> loadVectors(const std::filesystem::path& file);

/// Every `*.vec` file under `root`, found recursively and returned in a fixed (lexicographic)
/// order so a failing VEC-02 run is reproducible. Any path with a "captured" component is
/// excluded (HIL-capture output is not source-typed, SPEC-hil-capture.md); returns empty when
/// `root` does not exist.
std::vector<std::filesystem::path> findVectorFiles(const std::filesystem::path& root);

/// findVectorFiles(root), each loaded with loadVectors() and concatenated in the same order.
/// Throws VecFormatError on the first malformed file.
std::vector<Vector> loadAllVectors(const std::filesystem::path& root);

} // namespace mc::test
