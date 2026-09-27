/**
 * @file log.h
 * @brief The application log hook (`LogSink`) and a hex dump helper for debug logging (spec
 * `mc-protocol-frame-spec.md` §8.6).
 */
#pragma once

#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mc {

/**
 * @enum LogLevel
 * @brief Severity of one log line, in increasing order; `Off` disables logging entirely.
 */
enum class LogLevel : uint8_t {
    Trace, ///< Per-byte or per-field detail.
    Debug, ///< Per-frame or per-command detail (e.g. a hex dump).
    Info,  ///< Notable, expected events (e.g. a link connecting).
    Warn,  ///< Recoverable trouble (e.g. a retry).
    Error, ///< A request or link failed.
    Off    ///< Nothing is ever enabled at this level; use it to disable a category entirely.
};

/**
 * @class LogSink
 * @brief The one hook an application implements to receive the library's log lines.
 *
 * The library never formats a message unless enabled() has already said yes for that level: the
 * call pattern every caller in this library follows is `if (sink.enabled(level)) { build the
 * message in a stack buffer; sink.write(...); }`, so enabled()'s own cost is the only cost paid
 * for a disabled level. Implementations must be safe to call from any thread the library runs
 * on; the library itself takes no lock around a LogSink call.
 *
 * @see NullLogSink
 */
class LogSink {
public:
    virtual ~LogSink() = default;

    /**
     * @brief Whether @p level is currently of interest.
     *
     * Consulted before any message string is built; must be cheap (e.g. an atomic load) and
     * thread-safe.
     *
     * @param[in] level Level the caller is about to log at.
     * @return true when the caller should build and send the message.
     * @par Complexity
     * O(1); no allocation (contract on the implementer).
     * @see write
     */
    virtual bool enabled(LogLevel level) const noexcept = 0;

    /**
     * @brief Receives one log line.
     *
     * Only called after enabled(level) has returned true.
     *
     * @param[in] level Level this line was logged at.
     * @param[in] category Static string identifying the subsystem, e.g. "mc.session",
     * "mc.protocol"; valid forever.
     * @param[in] message The log line; valid only during this call.
     * @par Complexity
     * Implementation-defined; the library treats it as O(message.size()) and allocation-free,
     * but does not enforce either.
     * @see enabled
     */
    virtual void write(LogLevel level, std::string_view category, std::string_view message)
        noexcept = 0;
};

/**
 * @class NullLogSink
 * @brief A LogSink that disables every level and discards every line; the default when an
 * application plugs in nothing.
 *
 * @see LogSink
 */
class NullLogSink final : public LogSink {
public:
    /// @return false, always: nothing is ever enabled.
    bool enabled(LogLevel level) const noexcept override;

    /// No-op: never reached, since enabled() always returns false.
    void write(LogLevel level, std::string_view category,
               std::string_view message) noexcept override;
};

/**
 * @brief Renders @p bytes as a hex dump into @p out (spec §8.6): `"50 00 00 FF FF 03 00"`.
 *
 * Behaves like snprintf: writes at most `capacity - 1` characters plus a terminating NUL (writes
 * nothing when capacity == 0), and always returns the number of characters the full dump needs,
 * whether or not it fit in @p capacity.
 *
 * @param[in] bytes Bytes to render.
 * @param[out] out Destination buffer; may be null when capacity == 0.
 * @param[in] capacity Number of bytes available at @p out, including the terminating NUL.
 * @param[in] names When true, a byte equal to one of STX (02H), ETX (03H), ENQ (05H), ACK (06H),
 * NAK (15H), CR (0DH), LF (0AH) or DLE (10H) renders as its bracketed name (e.g. `<STX>`) instead
 * of two hex digits; every other byte, and every byte when @p names is false, renders as two
 * uppercase hex digits.
 * @return The number of characters the full dump needs, excluding the terminating NUL.
 * @par Complexity
 * O(n) in `bytes.size`; no allocation.
 */
size_t hexDump(ByteView bytes, char* out, size_t capacity, bool names = false) noexcept;

} // namespace mc
