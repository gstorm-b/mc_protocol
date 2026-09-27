// VEC-01 / VEC-02 (SPEC-core-protocol.md "Golden vector files", T-013): the `.vec` loader
// itself, and the transcription guard that runs over every checked-in vector file.
#include "doctest/doctest.h"

#include "common/vectors.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifndef MC_TESTS_SOURCE_DIR
#error "MC_TESTS_SOURCE_DIR not defined; see tests/CMakeLists.txt (mc_core_protocol_tests)"
#endif

using mc::test::loadAllVectors;
using mc::test::loadVectors;
using mc::test::parseVectors;
using mc::test::Vector;
using mc::test::VecFormatError;

namespace {

// tests/vectors, resolved from the source tree rather than the test binary's own working
// directory: ctest runs mc_core_protocol_tests from its own build tree, and the MSVC
// (build/cmake-debug) and MinGW (build/cmake-mingw) trees sit at different depths under the
// repository root, so a relative "../../tests/vectors" would not be portable between them.
std::filesystem::path vectorsRoot() {
    return std::filesystem::path(MC_TESTS_SOURCE_DIR) / "vectors";
}

const Vector& findById(const std::vector<Vector>& vectors, std::string_view id) {
    for (const auto& v : vectors) {
        if (v.id == id) {
            return v;
        }
    }
    FAIL("no vector with id ", id);
    static Vector dummy;
    return dummy;
}

} // namespace

TEST_CASE("VEC-01: sample.vec uses every syntax element and loads to the expected records") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "sample.vec");
    REQUIRE(vectors.size() == 5);

    const Vector& s1 = findById(vectors, "SAMPLE-01");
    CHECK(s1.field("frame") == "3E");
    CHECK(s1.field("code") == "Binary");
    CHECK(s1.field("op") == "ReadWords");
    CHECK(s1.field("device") == "D100");
    CHECK(s1.field("count") == "3");
    CHECK(s1.field("kind") == "request");
    CHECK(s1.field("bytes") == "21");
    REQUIRE(s1.bytes.size() == 21);
    CHECK(s1.bytes.front() == 0x50);
    CHECK(s1.bytes.back() == 0x00);
    CHECK_FALSE(s1.hasText);

    const Vector& s2 = findById(vectors, "SAMPLE-02");
    CHECK(s2.field("kind") == "response");
    CHECK(s2.field("of") == "SAMPLE-01");
    CHECK(s2.field("expect") == "words 1995 1202 1130");
    REQUIRE(s2.bytes.size() == 17);
    CHECK(s2.bytes.front() == 0xD0);

    const Vector& s3 = findById(vectors, "SAMPLE-03");
    CHECK(s3.field("op") == "WriteWords");
    CHECK(s3.field("write") == "1995 1202 1130");
    REQUIRE(s3.bytes.size() == 27);

    const Vector& s4 = findById(vectors, "SAMPLE-04");
    CHECK(s4.field("frame") == "3C");
    CHECK(s4.field("format") == "F1");
    CHECK(s4.hasTag("v2"));
    REQUIRE(s4.bytes.size() == 39);
    CHECK(s4.hasText);
    CHECK(s4.text == "<ENQ>F80000FF03FF000004010000D*000100000350");

    const Vector& s5 = findById(vectors, "SAMPLE-05");
    CHECK(s5.field("of") == "SAMPLE-04");
    CHECK(s5.hasTag("v2"));
    REQUIRE(s5.bytes.size() == 32);
    CHECK(s5.hasText);
    CHECK(s5.text == "<STX>F80000FF03FF0000199512021130<ETX>DE");
}

TEST_CASE("VEC-01 negative: odd hex digit count fails with file and line") {
    bool threw = false;
    try {
        parseVectors("# id: BAD-01\n"
                     "50 00 0\n",
                     "bad_odd_hex.vec");
    } catch (const VecFormatError& e) {
        threw = true;
        std::string what = e.what();
        CHECK(what.find("bad_odd_hex.vec:2") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("VEC-01 negative: unknown control name fails with file and line") {
    bool threw = false;
    try {
        parseVectors("# id: BAD-02\n"
                     "02 46\n"
                     "<XYZ>F\n",
                     "bad_unknown_control.vec");
    } catch (const VecFormatError& e) {
        threw = true;
        std::string what = e.what();
        CHECK(what.find("bad_unknown_control.vec:3") != std::string::npos);
        CHECK(what.find("XYZ") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("VEC-01 negative: a hex line with no preceding id fails with file and line") {
    bool threw = false;
    try {
        parseVectors("# kind: request\n"
                     "50 00\n",
                     "bad_no_id.vec");
    } catch (const VecFormatError& e) {
        threw = true;
        std::string what = e.what();
        CHECK(what.find("bad_no_id.vec:2") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("VEC-01 negative: a malformed bytes: value fails with file and line") {
    bool threw = false;
    try {
        parseVectors("# id: BAD-04\n"
                     "# bytes: abc\n"
                     "50 00\n",
                     "bad_bytes_value.vec");
    } catch (const VecFormatError& e) {
        threw = true;
        std::string what = e.what();
        CHECK(what.find("bad_bytes_value.vec:2") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("VEC-02 transcription guard: every vector's hex byte count matches its bytes: field") {
    std::vector<Vector> vectors = loadAllVectors(vectorsRoot());
    REQUIRE_FALSE(vectors.empty());

    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        std::string bytesField = v.field("bytes");
        REQUIRE_FALSE(bytesField.empty());
        size_t expected = static_cast<size_t>(std::stoul(bytesField));
        CHECK(v.bytes.size() == expected);
    }
}
