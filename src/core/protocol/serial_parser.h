/**
 * @file serial_parser.h
 * @brief Incremental receive state machine for the serial ASCII formats 1-4 of 3C and 1C (spec
 * `mc-protocol-frame-spec.md` §6.3, with §2.5 sum check ranges, §2.7 control codes and the §5.4
 * format table): finds where a response frame starts and ends, verifies its sum check and
 * terminator, and reports where its parts lie. It does not judge what the parts say (frame ID,
 * route, block number, data size, PLC error code): frame_serial.h does, on the layout returned
 * here.
 *
 * Bytes before the first start byte are junk: they are skipped and counted (spec §6.3 "START").
 * The caller keeps the receive buffer and calls serialFeed() with the whole buffer after every
 * receive; a SerialCursor remembers how far the previous call got, so the scan for ETX visits only
 * new bytes and a frame costs O(L) in total, however it was split across receives.
 *
 * Serial frames are always ASCII, so nothing here is templated on a codec.
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "mc/core/frame_config.h"
#include "mc/core/protocol.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>

namespace mc::detail {

/// Characters of the SUM and of CR LF (Format 4): behind ETX, or behind the fixed part of an ACK /
/// NAK.
inline constexpr size_t kSerialSumSize = 2;
inline constexpr size_t kSerialCrLfSize = 2;

/// A `Protocol` error with a static message, as every serial parse failure builds one (spec §7.2).
Error serialProtocolError(ErrorCode code, const char* message) noexcept;

/// Control codes of spec §2.7 that delimit a serial frame.
inline constexpr uint8_t kSerialStx = 0x02;
inline constexpr uint8_t kSerialEtx = 0x03;
inline constexpr uint8_t kSerialEnq = 0x05;
inline constexpr uint8_t kSerialAck = 0x06;
inline constexpr uint8_t kSerialLf = 0x0A;
inline constexpr uint8_t kSerialCr = 0x0D;
inline constexpr uint8_t kSerialNak = 0x15;

/// Characters of the frame ID plus access route `P` (spec §5.5, §5.6): 3C `"F9"` + station,
/// network, PC and self-station (2 each) = 10; 1C station + PC = 4 (no frame ID).
constexpr size_t serialRouteSize(FrameType frame) noexcept {
    return frame == FrameType::F1C ? 4 : 10;
}

/// Characters of a PLC error code (spec §5.4-§5.6): 3C 4 (`err4`), 1C 2 (`err2`). The Format 3 end
/// codes `"QACK"`/`"QNAK"` (3C) and `"GG"`/`"NN"` (1C) have the same size.
constexpr size_t serialCodeSize(FrameType frame) noexcept {
    return frame == FrameType::F1C ? 2 : 4;
}

/// Characters of the block number field: 2 in Format 2, none in the other formats.
constexpr size_t serialBlockSize(SerialFormat format) noexcept {
    return format == SerialFormat::Format2 ? 2 : 0;
}

/// What the parser must know in advance (spec §6.3: "frame type, format, sum check on/off").
struct SerialParams {
    FrameType frame{FrameType::F3C};            ///< F3C or F1C; picks sizes and Format 3 codes.
    SerialFormat format{SerialFormat::Format1}; ///< Formats 1-4; Format 5 is not parsed here.
    bool sumCheck{true};                        ///< Frames carry a 2-character SUM.
    bool f3ShortResponseHasSum{false};          ///< Format 3 responses without data carry a SUM.
};

/// The parser parameters a `FrameConfig` implies.
constexpr SerialParams serialParamsOf(const FrameConfig& cfg) noexcept {
    return SerialParams{cfg.frame, cfg.format, cfg.sumCheck, cfg.f3ShortResponseHasSum};
}

/// Offset of the response data from the start byte of a response that has data (STX): the start
/// byte, the block number (Format 2), `P` and, in Format 3, the end code `"QACK"`/`"GG"`.
constexpr size_t serialDataOffset(const SerialParams& p) noexcept {
    return 1 + serialBlockSize(p.format) + serialRouteSize(p.frame) +
           (p.format == SerialFormat::Format3 ? serialCodeSize(p.frame) : 0);
}

/// What kind of response a complete frame is.
enum class SerialResponseKind : uint8_t {
    Data, ///< Response with data: `STX ... data ETX [SUM]`; Format 3: end code `QACK`/`GG`.
    Ack,  ///< Response without data: `ACK P`; Format 3: `STX P QACK ETX`.
    Nak   ///< Error response: `NAK P err`; Format 3: `STX P QNAK err ETX`.
};

/// Where the parts of a complete response frame lie. All offsets count from the first byte of the
/// buffer handed to serialFeed(), so they include the skipped junk.
struct SerialFrame {
    SerialResponseKind kind{SerialResponseKind::Data}; ///< Data, Ack or Nak.
    size_t blockOffset{0}; ///< Format 2: the 2 block number characters; otherwise unused.
    size_t routeOffset{0}; ///< The frame ID plus access route `P`, `serialRouteSize()` characters.
    size_t dataOffset{0};  ///< Data: the response data; Nak: the error code; Ack: just past `P`
                           ///< (Format 3: past the end code).
    size_t dataSize{0};    ///< Data: characters of response data; Nak: `serialCodeSize()`; Ack: 0.
    size_t end{0};         ///< Bytes of the buffer the frame occupies: junk, frame and trailer.
};

/// Progress kept between serialFeed() calls; value-initialize it for a new frame.
///
/// While no start byte has been found, `scanned == skipped` (every byte so far is junk). Once it
/// is found, `skipped` is its offset and `scanned > skipped`. While the parser waits for the SUM
/// or CR LF behind ETX, `scanned` rests on the ETX itself, so a later call finds it again at once
/// and never re-reads the body.
struct SerialCursor {
    size_t scanned{0}; ///< Bytes of the buffer already examined.
    size_t skipped{0}; ///< Junk bytes before the start byte (spec §6.3 "START").
};

/**
 * @brief Advances the receive state machine over the new bytes of `buffer` (spec §6.3).
 *
 * The start byte is STX, ACK or NAK in Formats 1, 2 and 4 and only STX in Format 3; any other
 * byte before it is skipped. A response with data is read up to ETX, then its SUM (when enabled)
 * is read and verified and, in Format 4, CR LF. ACK and NAK frames have a fixed length (spec §6.3
 * "ACK", "NAK") and carry no SUM. A Format 3 frame is classified by its end code: with data it
 * carries a SUM when enabled; `QACK`/`GG` without data and `QNAK`/`NN` carry one only when
 * `SerialParams::f3ShortResponseHasSum` is set (spec §10 Q1). The SUM is summed from the byte after
 * STX through ETX in every format (spec §2.5).
 *
 * `Failed` with `error` set when the frame is malformed at this level: `SumCheck` for a wrong SUM,
 * `InvalidCharacter` for a SUM that is not hex, `FrameMismatch` for a Format 3 end code that is
 * neither expected string or a Format 4 frame not ending in CR LF, `LengthMismatch` for a body too
 * short for `P` and the end code (or a `QNAK` whose error code has the wrong size). `frame.end` is
 * then the bytes to discard: the whole frame once its end was found.
 *
 * @param[in] p Parameters of the expected frame.
 * @param[in] buffer Every byte received so far, from the first byte examined for this frame.
 * Bytes already passed in an earlier call must not change, and the buffer must not shrink between
 * calls: a caller that discards bytes calls `reset()` on its `Parser` (a fresh cursor) first.
 * @param[in,out] cursor Progress; value-initialized for the first call, then passed back as
 * returned.
 * @param[out] frame Set when the return value is `Done` or `Failed`.
 * @param[out] error Set when the return value is `Failed`.
 * @return `NeedMore`, `Done` (a complete frame of `frame.kind` ends at `frame.end`) or `Failed`.
 * @par Complexity
 * O(k) in the bytes newly examined since the previous call; no allocation.
 */
ParseStatus serialFeed(const SerialParams& p, ByteView buffer, SerialCursor& cursor,
                       SerialFrame& frame, Error& error) noexcept;

} // namespace mc::detail
