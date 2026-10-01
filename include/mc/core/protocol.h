/**
 * @file protocol.h
 * @brief Turns a validated `Request` into the exact bytes of one complete frame, and the bytes
 * of one response frame into a normalized payload or an `Error` (spec
 * `mc-protocol-frame-spec.md` §5, §7.2): `McProtocol`, `Parser`, `ParseStatus`.
 */
#pragma once

#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mc {

/**
 * @enum ParseStatus
 * @brief Outcome of one `Parser::feed()` call.
 * @see Parser::feed
 */
enum class ParseStatus : uint8_t {
    NeedMore, ///< Not enough bytes yet; keep receiving and call feed() again.
    Done,     ///< A complete, valid frame was found; see Parser::frameLength(), Parser::payload().
    Failed    ///< A malformed frame or a PLC error; see Parser::error(), Parser::frameLength().
};

/**
 * @class Parser
 * @brief Incremental response parser for exactly one request.
 *
 * A small value type (fixed size, no allocation of its own) that keeps a cursor over a buffer
 * THE CALLER OWNS. The caller appends received bytes to its own buffer and calls feed() with the
 * whole buffer each time; the parser examines only the bytes past what it has already found
 * conclusive. Bytes already fed must not change between calls.
 *
 * Constructed only through `McProtocol::parser()`; there is no public constructor.
 *
 * Trivially copyable, `sizeof(Parser) <= 128` (module spec "Internal design").
 *
 * @see McProtocol::parser, ParseStatus
 */
class Parser {
public:
    /**
     * @brief Advances over new bytes of `buffer`.
     *
     * Once a previous call has returned `Done` or `Failed`, further calls return the same
     * status again without re-examining `buffer` (call reset() first to parse a new frame for
     * the same request).
     *
     * @param[in] buffer Every byte received for this response so far, from the start of the
     * frame (not just the bytes new since the last call).
     * @retval ParseStatus::NeedMore `buffer` does not yet hold a complete frame; keep receiving.
     * @retval ParseStatus::Done `frameLength()` bytes of `buffer` form this frame; any further
     * bytes belong to the next one.
     * @retval ParseStatus::Failed `error()` is set; `frameLength()` is the number of bytes to
     * discard (the whole malformed frame when its own length field was still readable,
     * otherwise only the bytes examined before giving up).
     * @par Complexity
     * O(k) in bytes newly examined; total O(L) per frame; no allocation.
     * @see frameLength, error, reset
     */
    ParseStatus feed(ByteView buffer) noexcept;

    /**
     * @brief Bytes of the buffer this frame occupies.
     * @pre The last feed() call returned `Done` or `Failed`.
     * @return See feed()'s `Done`/`Failed` documentation for exactly what this counts.
     * @par Complexity
     * O(1); no allocation.
     * @see feed
     */
    size_t frameLength() const noexcept;

    /**
     * @brief Bytes skipped before this frame started.
     *
     * Always 0 for Ethernet frames (3E/4E) and 1E: they have no leading junk to skip. Serial
     * frames (3C/1C) count the bytes before the start byte (STX, ACK or NAK) here, for
     * diagnostics.
     *
     * @pre The last feed() call returned `Done` or `Failed`.
     * @return Number of bytes of junk skipped before the frame's own start.
     * @par Complexity
     * O(1); no allocation.
     */
    size_t skipped() const noexcept;

    /**
     * @brief Decodes this frame's response data into `out`, in the normalized layout of the
     * request `McProtocol::parser()` built this `Parser` from (protocol.h's own "Payload
     * contract", see `McProtocol::payloadSize()`): `ReadBits`/`BitLayout::BytePerPoint` one byte
     * per point; `ReadBits`/`BitLayout::PackedLsbFirst` `ceil(count / 8)` bytes; `ReadWords`
     * `2 * count` bytes, little-endian; a write's response carries no data (0 bytes written).
     *
     * @param[in] buffer The same bytes that produced `Done` from feed() (or a prefix/superset
     * that still contains the same frame at the same offset).
     * @param[out] out Destination; must hold at least `McProtocol::payloadSize()` bytes for the
     * request this parser answers.
     * @pre The last feed() call returned `Done`.
     * @return Bytes written to `out`.
     * @retval ErrorCode::BufferTooSmall `out` is smaller than the payload needs; nothing is
     * written.
     * @par Complexity
     * O(n) in the payload size; no allocation.
     * @see feed, McProtocol::payloadSize
     */
    Expected<size_t> payload(ByteView buffer, MutableByteView out) const noexcept;

    /**
     * @brief The error this frame failed with.
     * @pre The last feed() call returned `Failed`.
     * @return A `Protocol` error (malformed frame) or a `Plc` error (the PLC answered with a
     * non-zero end code), per spec §7.2.
     * @par Complexity
     * O(1); no allocation.
     * @see feed
     */
    const Error& error() const noexcept;

    /**
     * @brief Returns this `Parser` to its initial state, for the same request.
     * @post The next feed() call starts examining `buffer` from its first byte again, as if this
     * `Parser` had just been returned by `McProtocol::parser()`.
     * @par Complexity
     * O(1); no allocation.
     */
    void reset() noexcept;

private:
    friend class McProtocol;

    Parser() noexcept = default;

    FrameConfig m_cfg{};
    Op m_op{Op::ReadWords};
    uint16_t m_count{0};
    BitLayout m_bitLayout{BitLayout::BytePerPoint};
    ParseStatus m_status{ParseStatus::NeedMore};
    Error m_error{};
    size_t m_frameLength{0};
    size_t m_skipped{0};
};

static_assert(sizeof(Parser) <= 128, "Parser must stay a small, fixed-size value type (spec "
                                      "\"Internal design\")");
static_assert(std::is_trivially_copyable_v<Parser>, "Parser must stay trivially copyable");

/**
 * @class McProtocol
 * @brief Stateless codec for one `FrameConfig`.
 *
 * Value type, copyable, cheap to construct. Turns a `Request` into the exact bytes of one
 * complete frame (`encode()`), and builds a `Parser` (`parser()`) that turns the bytes of one
 * response frame into a normalized payload or an `Error`.
 *
 * A frame family/wire code this library does not implement yet always fails the same way, never
 * undefined behaviour: `encodedSize()` and `encode()` return `ErrorCode::UnsupportedCommand`;
 * `maxResponseSize()` returns 0; `parser()` returns a `Parser` whose first feed() call returns
 * `ParseStatus::Failed` with `ErrorCode::UnsupportedCommand`. `payloadSize()` alone is unaffected
 * (see its own doc: it never depends on the frame at all).
 *
 * @see Parser, ParseStatus
 */
class McProtocol {
public:
    /**
     * @brief Constructs a codec bound to `cfg`.
     * @param[in] cfg Frame configuration this codec encodes/parses. Copied; kept for the
     * lifetime of this `McProtocol`.
     * @pre `cfg.validate()` is `Ok`. A `cfg` that fails `validate()` makes every `encode()` call
     * return `cfg.validate()`'s own error rather than producing bytes (checked again inside
     * `encode()`/`encodedSize()`; not just at construction).
     * @par Complexity
     * O(1); no allocation.
     */
    explicit McProtocol(const FrameConfig& cfg) noexcept;

    /**
     * @brief The frame configuration this codec was constructed with.
     * @return A reference valid for the lifetime of this `McProtocol`.
     * @par Complexity
     * O(1); no allocation.
     */
    const FrameConfig& config() const noexcept;

    /**
     * @brief Wire size of the complete request frame for `r`.
     *
     * Runs `validate(r, config())` first (core-model).
     *
     * @param[in] r Request that would be encoded.
     * @return Bytes the complete frame needs.
     * @retval ErrorCode::UnsupportedCommand `config().frame`/`config().code` is not implemented
     * yet.
     * @par Complexity
     * O(1); no allocation.
     * @see encode
     */
    Expected<size_t> encodedSize(const Request& r) const noexcept;

    /**
     * @brief Encodes `r` as ONE complete frame into `out` (no chunking; the caller chunks with
     * `mc::chunk()` first for a request wider than one frame's field maximum).
     *
     * Runs `validate(r, config())` first (core-model): a `Request`/`FrameConfig` pair
     * `validate()` rejects is never partially encoded.
     *
     * @param[in] r Request to encode.
     * @param[out] out Destination; must hold at least `encodedSize(r)` bytes.
     * @return Bytes written.
     * @retval ErrorCode::BufferTooSmall `out` is smaller than `encodedSize(r)`.
     * @retval ErrorCode::UnsupportedCommand `config().frame`/`config().code` is not implemented
     * yet.
     * @retval ErrorCode::InvalidConfig `config().frame` is `F1C` and `config().messageWait` is above
     * 15 (the one config value that would put a non-hex character on the wire; the other
     * `FrameConfig::validate()` rules are the caller's).
     * @par Complexity
     * O(n) in `r.count`; no allocation.
     * @see encodedSize
     */
    Expected<size_t> encode(const Request& r, MutableByteView out) const noexcept;

    /**
     * @brief Convenience overload: encodes `r` into a freshly allocated buffer.
     * @param[in] r Request to encode.
     * @return A buffer holding exactly `encodedSize(r)` bytes.
     * @par Complexity
     * O(n) in `r.count`; allocates one `ByteBuf`.
     * @see encode
     */
    Expected<ByteBuf> encode(const Request& r) const;

    /**
     * @brief Upper bound of the wire size of any successful or error response to `r`, so a
     * caller can size its receive buffer once, before sending.
     * @param[in] r Request whose response is being sized.
     * @return The upper bound, in bytes; 0 when `config().frame`/`config().code` is not
     * implemented yet.
     * @par Complexity
     * O(1); no allocation.
     * @see Parser::feed
     */
    size_t maxResponseSize(const Request& r) const noexcept;

    /**
     * @brief Size of the normalized payload of a successful response to `r` (the "Payload
     * contract": `ReadBits`/`BytePerPoint` one byte per point; `ReadBits`/`PackedLsbFirst`
     * `ceil(count / 8)` bytes; `ReadWords` `2 * count` bytes; a write's response is 0 bytes).
     *
     * Frame-independent: every frame family normalizes a response the same way, so this never
     * fails and never depends on `config()`.
     *
     * @param[in] r Request whose response payload is being sized.
     * @return The payload size, in bytes.
     * @par Complexity
     * O(1); no allocation.
     * @see Parser::payload
     */
    size_t payloadSize(const Request& r) const noexcept;

    /**
     * @brief Builds a `Parser` for the response to `r`, bound to this codec's configuration.
     * @param[in] r Request whose response the returned `Parser` will parse.
     * @return A `Parser` ready for its first feed() call.
     * @par Complexity
     * O(1); no allocation.
     * @see Parser
     */
    Parser parser(const Request& r) const noexcept;

private:
    FrameConfig m_config;
};

} // namespace mc
