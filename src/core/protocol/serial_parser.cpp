// src/core/protocol/serial_parser.cpp -- spec section 6.3 (states START, STX-BODY, ACK, NAK,
// F3-BODY), with the sum check ranges of section 2.5.
#include "serial_parser.h"

#include "hexascii.h"
#include "sumcheck.h"

namespace mc::detail {
namespace {

// Format 3 end codes (spec 5.4 table; 1C: 5.6).
constexpr uint8_t kAck3C[] = {'Q', 'A', 'C', 'K'};
constexpr uint8_t kNak3C[] = {'Q', 'N', 'A', 'K'};
constexpr uint8_t kAck1C[] = {'G', 'G'};
constexpr uint8_t kNak1C[] = {'N', 'N'};

bool isStartByte(const SerialParams& p, uint8_t c) noexcept {
    if (c == kSerialStx) {
        return true;
    }
    return p.format != SerialFormat::Format3 && (c == kSerialAck || c == kSerialNak);
}

bool matches(const uint8_t* at, const uint8_t* literal, size_t size) noexcept {
    for (size_t i = 0; i < size; ++i) {
        if (at[i] != literal[i]) {
            return false;
        }
    }
    return true;
}

// Verifies the 2 SUM characters behind ETX against the bytes after STX through ETX (`start` and
// `etx` are the offsets of STX and ETX). Returns an Ok error when they agree.
Error verifySum(const uint8_t* buffer, size_t start, size_t etx) noexcept {
    uint8_t computed = sumcheck(ByteView{buffer + start + 1, etx - start});
    uint8_t received = 0;
    auto decoded =
        hexDecode(ByteView{buffer + etx + 1, kSerialSumSize}, MutableByteView{&received, 1});
    if (!decoded.hasValue()) {
        return decoded.error();
    }
    if (received != computed) {
        return serialProtocolError(ErrorCode::SumCheck, "serial sum check mismatch");
    }
    return Error{};
}

ParseStatus fail(SerialFrame& frame, Error& error, size_t end, const Error& e) noexcept {
    frame.end = end;
    error = e;
    return ParseStatus::Failed;
}

// ACK and NAK frames of Formats 1, 2 and 4: a fixed length, no SUM (spec 6.3 "ACK", "NAK").
ParseStatus feedAckNak(const SerialParams& p, ByteView buffer, SerialCursor& cursor,
                       SerialFrame& frame, Error& error) noexcept {
    const uint8_t* b = buffer.data;
    const size_t start = cursor.skipped;
    const bool isNak = b[start] == kSerialNak;
    const bool hasCrLf = p.format == SerialFormat::Format4;

    size_t need = 1 + serialBlockSize(p.format) + serialRouteSize(p.frame) +
                  (isNak ? serialCodeSize(p.frame) : 0) + (hasCrLf ? kSerialCrLfSize : 0);
    if (buffer.size - start < need) {
        cursor.scanned = buffer.size;
        return ParseStatus::NeedMore;
    }
    const size_t end = start + need;
    if (hasCrLf && (b[end - 2] != kSerialCr || b[end - 1] != kSerialLf)) {
        return fail(
            frame, error, end,
            serialProtocolError(ErrorCode::FrameMismatch, "serial frame does not end in CR LF"));
    }
    frame.kind = isNak ? SerialResponseKind::Nak : SerialResponseKind::Ack;
    frame.dataOffset = frame.routeOffset + serialRouteSize(p.frame);
    frame.dataSize = isNak ? serialCodeSize(p.frame) : 0;
    frame.end = end;
    return ParseStatus::Done;
}

// STX frames: responses with data in Formats 1, 2 and 4, and every Format 3 response.
ParseStatus feedStx(const SerialParams& p, ByteView buffer, SerialCursor& cursor,
                    SerialFrame& frame, Error& error) noexcept {
    const uint8_t* b = buffer.data;
    const size_t n = buffer.size;
    const size_t start = cursor.skipped;
    const size_t bodyBegin = start + 1;

    // ASCII data never contains 03H, so the first ETX ends the body (spec 6.3); only bytes not
    // examined before are searched.
    size_t i = cursor.scanned > bodyBegin ? cursor.scanned : bodyBegin;
    while (i < n && b[i] != kSerialEtx) {
        ++i;
    }
    if (i == n) {
        cursor.scanned = n;
        return ParseStatus::NeedMore;
    }
    const size_t etx = i;
    const size_t bodySize = etx - bodyBegin;
    const size_t route = serialRouteSize(p.frame);
    const size_t code = serialCodeSize(p.frame);

    bool hasSum = p.sumCheck;
    size_t crLf = 0;
    if (p.format == SerialFormat::Format3) {
        // The end code decides whether the frame has data and so whether a SUM follows ETX.
        if (bodySize < route + code) {
            return fail(frame, error, etx + 1,
                        serialProtocolError(ErrorCode::LengthMismatch,
                                            "Format 3 body shorter than route and end code"));
        }
        const uint8_t* at = b + bodyBegin + route;
        const bool is1C = p.frame == FrameType::F1C;
        const uint8_t* ack = is1C ? kAck1C : kAck3C;
        const uint8_t* nak = is1C ? kNak1C : kNak3C;
        const size_t rest = bodySize - route - code;
        frame.dataOffset = bodyBegin + route + code;
        if (matches(at, ack, code)) {
            frame.kind = rest > 0 ? SerialResponseKind::Data : SerialResponseKind::Ack;
            frame.dataSize = rest;
        } else if (matches(at, nak, code)) {
            if (rest != code) {
                return fail(frame, error, etx + 1,
                            serialProtocolError(ErrorCode::LengthMismatch,
                                                "Format 3 error code has the wrong size"));
            }
            frame.kind = SerialResponseKind::Nak;
            frame.dataSize = code;
        } else {
            return fail(
                frame, error, etx + 1,
                serialProtocolError(ErrorCode::FrameMismatch,
                                    "Format 3 end code is neither the ACK nor the NAK string"));
        }
        if (frame.kind != SerialResponseKind::Data) {
            hasSum = hasSum && p.f3ShortResponseHasSum; // spec 10 Q1
        }
    } else {
        crLf = p.format == SerialFormat::Format4 ? kSerialCrLfSize : 0;
    }

    const size_t end = etx + 1 + (hasSum ? kSerialSumSize : 0) + crLf;
    if (n < end) {
        cursor.scanned = etx; // Waiting for SUM or CR LF: the next call finds this ETX at once.
        return ParseStatus::NeedMore;
    }

    if (hasSum) {
        Error e = verifySum(b, start, etx);
        if (!e.ok()) {
            return fail(frame, error, end, e);
        }
    }
    if (crLf != 0 && (b[end - 2] != kSerialCr || b[end - 1] != kSerialLf)) {
        return fail(
            frame, error, end,
            serialProtocolError(ErrorCode::FrameMismatch, "serial frame does not end in CR LF"));
    }

    if (p.format != SerialFormat::Format3) {
        const size_t head = serialBlockSize(p.format) + route;
        if (bodySize < head) {
            return fail(frame, error, end,
                        serialProtocolError(ErrorCode::LengthMismatch,
                                            "response body shorter than the access route"));
        }
        frame.kind = SerialResponseKind::Data;
        frame.dataOffset = bodyBegin + head;
        frame.dataSize = bodySize - head;
    }
    frame.end = end;
    return ParseStatus::Done;
}

} // namespace

Error serialProtocolError(ErrorCode code, const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Protocol;
    e.code = code;
    e.message = message;
    return e;
}

ParseStatus serialFeed(const SerialParams& p, ByteView buffer, SerialCursor& cursor,
                       SerialFrame& frame, Error& error) noexcept {
    if (cursor.scanned <= cursor.skipped) {
        // No start byte yet: everything examined so far is junk.
        size_t i = cursor.skipped;
        while (i < buffer.size && !isStartByte(p, buffer.data[i])) {
            ++i;
        }
        cursor.skipped = i;
        if (i == buffer.size) {
            cursor.scanned = i;
            return ParseStatus::NeedMore;
        }
        cursor.scanned = i + 1;
    }

    frame = SerialFrame{};
    frame.blockOffset = cursor.skipped + 1;
    frame.routeOffset = cursor.skipped + 1 + serialBlockSize(p.format);

    if (buffer.data[cursor.skipped] == kSerialStx) {
        return feedStx(p, buffer, cursor, frame, error);
    }
    return feedAckNak(p, buffer, cursor, frame, error);
}

} // namespace mc::detail
