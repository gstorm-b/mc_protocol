#include "doctest/doctest.h"

#include "mc/core/result.h"

#include <memory>
#include <string_view>
#include <type_traits>

// Acceptance criterion 3 (T-006): Error and Expected<int> are trivially copyable. The binding
// invariant lives as static_assert in mc/core/result.h itself (compiled by every translation
// unit that includes it, including this one); restated here so the check is visible next to the
// other RES tests instead of only inside the header.
static_assert(std::is_trivially_copyable_v<mc::Error>, "Error must be trivially copyable");
static_assert(std::is_trivially_copyable_v<mc::Expected<int>>,
              "Expected<int> must be trivially copyable");

TEST_CASE("RES-01 Expected<T> holds a value and reports it") {
    mc::Expected<int> e(42);
    CHECK(e.hasValue());
    CHECK(static_cast<bool>(e));
    CHECK(e.value() == 42);

    const mc::Expected<int>& ce = e;
    CHECK(ce.value() == 42);
}

TEST_CASE("RES-02 Expected<T> holds an error and reports it") {
    mc::Error err{};
    err.category = mc::ErrorCategory::Config;
    err.code = mc::ErrorCode::InvalidConfig;
    err.message = "bad config";

    mc::Expected<int> e(err);
    CHECK_FALSE(e.hasValue());
    CHECK_FALSE(static_cast<bool>(e));
    CHECK(e.error().category == mc::ErrorCategory::Config);
    CHECK(e.error().code == mc::ErrorCode::InvalidConfig);
    CHECK_FALSE(e.error().ok());
    CHECK(std::string_view(e.error().message) == "bad config");
}

TEST_CASE("RES-03 Expected<void> value/error semantics; Error::ok(); message is static") {
    mc::Expected<void> ok;
    CHECK(ok.hasValue());
    CHECK(static_cast<bool>(ok));

    mc::Error err{};
    err.category = mc::ErrorCategory::Encode;
    err.code = mc::ErrorCode::PointCount;
    err.message = "bad count";

    mc::Expected<void> fail(err);
    CHECK_FALSE(fail.hasValue());
    CHECK_FALSE(static_cast<bool>(fail));
    CHECK(fail.error().code == mc::ErrorCode::PointCount);
    CHECK_FALSE(fail.error().ok());

    // Error::ok().
    mc::Error okErr{};
    CHECK(okErr.code == mc::ErrorCode::Ok);
    CHECK(okErr.ok());
    mc::Error notOkErr{};
    notOkErr.code = mc::ErrorCode::Timeout;
    CHECK_FALSE(notOkErr.ok());

    // message is a static string: a string literal assigned to it survives being read back
    // through the plain `const char*` field, with no copy or allocation by Error itself.
    static constexpr const char* kMsg = "static message";
    mc::Error withMessage{};
    withMessage.message = kMsg;
    CHECK(withMessage.message == kMsg);
    CHECK(std::string_view(withMessage.message) == "static message");
}

TEST_CASE("RES-04 Expected<T> with a move-only T moves the value in and out") {
    mc::Expected<std::unique_ptr<int>> e(std::make_unique<int>(7));
    REQUIRE(e.hasValue());
    CHECK(*e.value() == 7);

    std::unique_ptr<int> out = std::move(e.value());
    REQUIRE(out != nullptr);
    CHECK(*out == 7);
}

TEST_CASE("RES-04 Expected<T> with a move-only T also carries an Error") {
    mc::Error err{};
    err.category = mc::ErrorCategory::Encode;
    err.code = mc::ErrorCode::BufferTooSmall;

    mc::Expected<std::unique_ptr<int>> e(err);
    CHECK_FALSE(e.hasValue());
    CHECK(e.error().code == mc::ErrorCode::BufferTooSmall);
}

TEST_CASE("RES-04 copying Expected<T> with a move-only T does not compile") {
    using MoveOnlyExpected = mc::Expected<std::unique_ptr<int>>;
    static_assert(!std::is_copy_constructible_v<MoveOnlyExpected>,
                  "Expected<T> with move-only T must not be copy constructible");
    static_assert(!std::is_copy_assignable_v<MoveOnlyExpected>,
                  "Expected<T> with move-only T must not be copy assignable");
    static_assert(std::is_move_constructible_v<MoveOnlyExpected>,
                  "Expected<T> with move-only T must still be move constructible");
    static_assert(std::is_move_assignable_v<MoveOnlyExpected>,
                  "Expected<T> with move-only T must still be move assignable");
}
