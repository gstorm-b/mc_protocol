/**
 * @file result.h
 * @brief Error vocabulary (`ErrorCategory`, `ErrorCode`, `ErrorInfo`, `Error`) and the
 * value-based `Expected<T>` / `Expected<void>` this library returns instead of throwing.
 */
#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

namespace mc {

/**
 * @enum ErrorCategory
 * @brief Broad origin of an Error; ErrorCode narrows it to a specific condition.
 */
enum class ErrorCategory : uint8_t {
    None,      ///< No error (paired with ErrorCode::Ok).
    Config,    ///< A FrameConfig or Request is invalid before anything was sent.
    Encode,    ///< The request could not be turned into bytes.
    Transport, ///< The underlying link failed to deliver bytes.
    Protocol,  ///< The response did not parse as a valid frame.
    Plc        ///< The PLC itself answered with an error end code.
};

/**
 * @enum ErrorCode
 * @brief Specific error condition; grouped by ErrorCategory in declaration order (Config,
 * Encode, Transport, Protocol, Plc).
 */
enum class ErrorCode : uint16_t {
    Ok = 0, ///< No error.

    InvalidConfig, ///< Config: a FrameConfig field is out of range or contradictory.
    NotSubscribed, ///< Config: core-session ValueStore/RangeSet operation on a device that was
                   ///< never subscribed.

    InvalidDevice,      ///< Encode: unsupported device/symbol for the frame family, or a device
                        ///< number outside the family's field width.
    PointCount,         ///< Encode: count is zero, or exceeds the frame's field/table maximum.
    UnsupportedCommand, ///< Encode: no encoding exists for this Op on this frame.
    DataSizeMismatch,   ///< Encode: write payload size does not match count and BitLayout.
    BufferTooSmall,     ///< Encode: caller-supplied output buffer/array has too little capacity.

    Timeout,   ///< Transport: no response within the frame's effective timeout.
    LinkDown,  ///< Transport: the transport reported the link is not usable.
    QueueFull, ///< Transport: core-session request queue is at capacity.

    FrameMismatch,    ///< Protocol: response does not match the request's frame type/subheader.
    LengthMismatch,   ///< Protocol: declared length field does not match the bytes received.
    SumCheck,         ///< Protocol: serial frame's sum check failed.
    InvalidCharacter, ///< Protocol: ASCII frame contains a byte outside its character set.

    PlcError ///< Plc: the PLC answered with a non-zero end code; see Error::plcCode and
             ///< Error::info.
};

/**
 * @struct ErrorInfo
 * @brief 3E/4E error information block, present only when the frame is 3E/4E and the PLC
 * answered with a non-zero end code (spec `mc-protocol-frame-spec.md` §5.1, field 8b).
 */
struct ErrorInfo {
    uint8_t network{0};     ///< Network number echoed back by the PLC.
    uint8_t pc{0};          ///< PC number echoed back by the PLC.
    uint16_t io{0};         ///< Request destination module I/O number echoed back by the PLC.
    uint8_t station{0};     ///< Request destination module station number echoed back by the PLC.
    uint16_t command{0};    ///< Command that produced the error.
    uint16_t subcommand{0}; ///< Subcommand that produced the error.
};

/**
 * @struct Error
 * @brief One error outcome: a category, a specific code, protocol-specific detail, and a static
 * message.
 *
 * Value type, trivially copyable, small enough to pass by value; never carries an allocation of
 * its own. Detailed context (a hex dump, a route) never goes here — it goes to the LogSink at the
 * point of failure.
 *
 * @see ErrorCategory, ErrorCode, ErrorInfo, Expected
 */
struct Error {
    ErrorCategory category{ErrorCategory::None}; ///< Broad origin of this error.
    ErrorCode code{ErrorCode::Ok};                ///< Specific condition; Ok means no error.
    uint16_t plcCode{0};      ///< Raw end code / error code from the PLC (spec §7.2); 0 unless
                              ///< category == Plc.
    uint8_t abnormalCode{0};  ///< 1E only: abnormal code when the end code is 5BH.
    ErrorInfo info{};         ///< 3E/4E only; default-constructed (all zero) otherwise.
    const char* message{""};  ///< Static string literal, never allocated; safe to keep forever.

    /**
     * @brief Whether this Error represents success.
     * @return true when code == ErrorCode::Ok.
     * @par Complexity
     * O(1); no allocation.
     */
    constexpr bool ok() const noexcept { return code == ErrorCode::Ok; }
};

static_assert(std::is_trivially_copyable_v<Error>,
              "Error must stay trivially copyable (spec: Boundaries, Always)");

/**
 * @class Expected
 * @brief Minimal expected-or-error: either a `T` or an `Error`, with value semantics and no
 * allocation of its own.
 *
 * `T` may be move-only (e.g. `core-session` returns `Expected<Session>`); `Expected` declares no
 * copy/move members of its own, so it is copyable exactly when `T` is, and move-only when `T` is.
 *
 * @see Error, Expected<void>
 */
template <class T> class Expected {
public:
    /**
     * @brief Constructs a successful result, moving @p v in.
     * @param[in] v The value to hold.
     * @par Complexity
     * Same as T's move constructor; no allocation of its own.
     */
    Expected(T v) noexcept : m_storage(std::move(v)) {}

    /**
     * @brief Constructs a failed result.
     * @param[in] e The error to hold.
     * @par Complexity
     * O(1); no allocation.
     */
    Expected(Error e) noexcept : m_storage(e) {}

    /**
     * @brief Whether this holds a value rather than an Error.
     * @return true when this holds a value, false when it holds an Error.
     * @par Complexity
     * O(1); no allocation.
     */
    bool hasValue() const noexcept { return m_storage.index() == 0; }

    /**
     * @brief Same as hasValue(), for use in an `if`/boolean context.
     * @return Same as hasValue().
     * @par Complexity
     * O(1); no allocation.
     */
    explicit operator bool() const noexcept { return hasValue(); }

    /**
     * @brief Accesses the held value.
     * @pre hasValue()
     * @return The held value.
     * @par Complexity
     * O(1); no allocation.
     */
    T& value() noexcept { return std::get<T>(m_storage); }

    /**
     * @brief Accesses the held value.
     * @pre hasValue()
     * @return The held value.
     * @par Complexity
     * O(1); no allocation.
     */
    const T& value() const noexcept { return std::get<T>(m_storage); }

    /**
     * @brief Accesses the held error.
     * @pre !hasValue()
     * @return The held error.
     * @par Complexity
     * O(1); no allocation.
     */
    const Error& error() const noexcept { return std::get<Error>(m_storage); }

private:
    std::variant<T, Error> m_storage;
};

static_assert(std::is_trivially_copyable_v<Expected<int>>,
              "Expected<T> must be trivially copyable when T is (spec acceptance criterion)");

/**
 * @class Expected<void>
 * @brief Specialization of Expected for functions with no success value: either success or an
 * Error.
 *
 * Same surface as the primary template, without value().
 *
 * @see Error, Expected
 */
template <> class Expected<void> {
public:
    /**
     * @brief Constructs a successful result.
     * @par Complexity
     * O(1); no allocation.
     */
    constexpr Expected() noexcept = default;

    /**
     * @brief Constructs a failed result.
     * @param[in] e The error to hold.
     * @par Complexity
     * O(1); no allocation.
     */
    constexpr Expected(Error e) noexcept : m_error(e) {}

    /**
     * @brief Whether this holds success rather than an Error.
     * @return true when this holds success, false when it holds an Error.
     * @par Complexity
     * O(1); no allocation.
     */
    constexpr bool hasValue() const noexcept { return m_error.ok(); }

    /**
     * @brief Same as hasValue(), for use in an `if`/boolean context.
     * @return Same as hasValue().
     * @par Complexity
     * O(1); no allocation.
     */
    constexpr explicit operator bool() const noexcept { return hasValue(); }

    /**
     * @brief Accesses the held error.
     * @pre !hasValue()
     * @return The held error.
     * @par Complexity
     * O(1); no allocation.
     */
    constexpr const Error& error() const noexcept { return m_error; }

private:
    Error m_error{};
};

} // namespace mc
