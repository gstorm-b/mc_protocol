// src/core/protocol/frame_serial.cpp -- spec sections 5.4 (format table, checks when parsing), 5.5
// (3C), 5.6 (1C) and 2.5 (sum check ranges). 3C and 1C share the envelope: they differ in the
// access route `P`, the command layer behind the request data, and the size of the PLC error code.
#include "frame_serial.h"

#include "command_a1c.h"
#include "sumcheck.h"

#include <algorithm>

namespace mc::detail {
namespace {

// Frame ID of 3C (spec 5.5): "F9".
constexpr uint8_t kFrameId3C[] = {'F', '9'};

Error plcError(uint16_t errorCode) noexcept {
    Error e{};
    e.category = ErrorCategory::Plc;
    e.code = ErrorCode::PlcError;
    e.plcCode = errorCode;
    e.message = "PLC answered a serial request with NAK";
    return e;
}

// Writes the access route `P`: 1C (spec 5.6) station PC; 3C (spec 5.5) the frame ID "F9" then
// station network PC self-station.
void putRoute(const FrameConfig& cfg, uint8_t* at) noexcept {
    if (cfg.frame == FrameType::F1C) {
        (void)AsciiCodec::putU8(cfg.stationNo, MutableByteView{at, AsciiCodec::u8Size()});
        (void)AsciiCodec::putU8(cfg.pc, MutableByteView{at + 2, AsciiCodec::u8Size()});
        return;
    }
    at[0] = kFrameId3C[0];
    at[1] = kFrameId3C[1];
    (void)AsciiCodec::putU8(cfg.stationNo, MutableByteView{at + 2, AsciiCodec::u8Size()});
    (void)AsciiCodec::putU8(cfg.network, MutableByteView{at + 4, AsciiCodec::u8Size()});
    (void)AsciiCodec::putU8(cfg.pc, MutableByteView{at + 6, AsciiCodec::u8Size()});
    (void)AsciiCodec::putU8(cfg.selfStation, MutableByteView{at + 8, AsciiCodec::u8Size()});
}

// Checks the frame ID (3C only; 1C has none), then (when cfg.checkRoute) the access route, of the
// response at `at`. Returns an Ok error when both match.
Error checkRoute(const FrameConfig& cfg, const uint8_t* at) noexcept {
    const bool is1C = cfg.frame == FrameType::F1C;
    if (!is1C && (at[0] != kFrameId3C[0] || at[1] != kFrameId3C[1])) {
        return serialProtocolError(ErrorCode::FrameMismatch, "3C frame ID is not F9");
    }
    if (!cfg.checkRoute) {
        return Error{};
    }
    const uint8_t expected3C[] = {cfg.stationNo, cfg.network, cfg.pc, cfg.selfStation};
    const uint8_t expected1C[] = {cfg.stationNo, cfg.pc};
    const uint8_t* expected = is1C ? expected1C : expected3C;
    const size_t fields = is1C ? sizeof(expected1C) : sizeof(expected3C);
    const size_t first = is1C ? 0 : 2; // characters of the frame ID
    for (size_t i = 0; i < fields; ++i) {
        auto field = AsciiCodec::getU8(ByteView{at + first + 2 * i, AsciiCodec::u8Size()});
        if (!field.hasValue()) {
            return field.error();
        }
        if (field.value() != expected[i]) {
            return serialProtocolError(ErrorCode::FrameMismatch,
                                       "serial response access route mismatch");
        }
    }
    return Error{};
}

// The command layer behind the envelope: QnA 0401/1401 for 3C, BR/WR/BW/WW (or JR/QR/JW/QW) for 1C.
size_t requestDataSize(const Request& r, const FrameConfig& cfg) noexcept {
    return cfg.frame == FrameType::F1C ? a1cRequestDataSize(r, cfg.commandSet)
                                       : qnaRequestDataSize<AsciiCodec>(r, cfg.series);
}

Expected<size_t> requestData(const Request& r, const FrameConfig& cfg,
                             MutableByteView out) noexcept {
    return cfg.frame == FrameType::F1C
               ? a1cRequestData(r, cfg.commandSet, cfg.messageWait, out, cfg.xyAsciiDigits)
               : qnaRequestData<AsciiCodec>(r, cfg.series, out, cfg.xyAsciiDigits);
}

size_t responseDataSize(const Request& r, const FrameConfig& cfg) noexcept {
    return cfg.frame == FrameType::F1C ? a1cResponseDataSize(r)
                                       : qnaResponseDataSize<AsciiCodec>(r);
}

// Format 2: the block number of the response equals the request's (spec 5.4, Q2), unless
// cfg.checkBlockNo is off. Returns an Ok error when it does.
Error checkBlock(const FrameConfig& cfg, const uint8_t* at) noexcept {
    if (!cfg.checkBlockNo) {
        return Error{};
    }
    auto block = AsciiCodec::getU8(ByteView{at, AsciiCodec::u8Size()});
    if (!block.hasValue()) {
        return block.error();
    }
    if (block.value() != cfg.blockNo) {
        return serialProtocolError(ErrorCode::FrameMismatch,
                                   "serial response block number mismatch");
    }
    return Error{};
}

} // namespace

size_t frameSerialEncodedSize(const Request& r, const FrameConfig& cfg) noexcept {
    // Start byte, [BLK], P, request data, [ETX in Format 3], [SUM], [CR LF in Format 4].
    size_t size =
        1 + serialBlockSize(cfg.format) + serialRouteSize(cfg.frame) + requestDataSize(r, cfg);
    if (cfg.format == SerialFormat::Format3) {
        size += 1;
    }
    if (cfg.sumCheck) {
        size += kSerialSumSize;
    }
    if (cfg.format == SerialFormat::Format4) {
        size += kSerialCrLfSize;
    }
    return size;
}

Expected<size_t> frameSerialEncode(const Request& r, const FrameConfig& cfg,
                                   MutableByteView out) noexcept {
    if (out.size < frameSerialEncodedSize(r, cfg)) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }

    const bool isFormat3 = cfg.format == SerialFormat::Format3;
    size_t offset = 0;
    out.data[offset++] = isFormat3 ? kSerialStx : kSerialEnq;
    if (cfg.format == SerialFormat::Format2) {
        (void)AsciiCodec::putU8(cfg.blockNo,
                                MutableByteView{out.data + offset, AsciiCodec::u8Size()});
        offset += serialBlockSize(cfg.format);
    }

    putRoute(cfg, out.data + offset);
    offset += serialRouteSize(cfg.frame);

    auto dataResult = requestData(r, cfg, MutableByteView{out.data + offset, out.size - offset});
    if (!dataResult.hasValue()) {
        return dataResult;
    }
    offset += dataResult.value();

    if (isFormat3) {
        out.data[offset++] = kSerialEtx;
    }
    if (cfg.sumCheck) {
        // The sum starts right after the start byte in every format (Format 2: at BLK) and runs
        // to the end of the request data, ETX included in Format 3 (spec 2.5).
        (void)sumcheckEncode(ByteView{out.data + 1, offset - 1},
                             MutableByteView{out.data + offset, kSerialSumSize});
        offset += kSerialSumSize;
    }
    if (cfg.format == SerialFormat::Format4) {
        out.data[offset++] = kSerialCr;
        out.data[offset++] = kSerialLf;
    }
    return Expected<size_t>(offset);
}

size_t frameSerialMaxResponseSize(const Request& r, const FrameConfig& cfg) noexcept {
    const size_t route = serialRouteSize(cfg.frame);
    const size_t code = serialCodeSize(cfg.frame);
    const size_t dataSize = responseDataSize(r, cfg);
    const size_t sum = cfg.sumCheck ? kSerialSumSize : 0;
    size_t data;
    size_t ack;
    size_t nak;
    if (cfg.format == SerialFormat::Format3) {
        // STX P QACK data ETX [SUM]; STX P QACK ETX [SUM]; STX P QNAK err ETX [SUM]. The short
        // forms carry a SUM only under f3ShortResponseHasSum (spec 10 Q1).
        const size_t shortSum = cfg.f3ShortResponseHasSum ? sum : 0;
        data = 1 + route + code + dataSize + 1 + sum;
        ack = 1 + route + code + 1 + shortSum;
        nak = 1 + route + code + code + 1 + shortSum;
    } else {
        // STX [BLK] P data ETX [SUM] [CR LF]; ACK [BLK] P [CR LF]; NAK [BLK] P err [CR LF].
        const size_t head = 1 + serialBlockSize(cfg.format) + route;
        const size_t crlf = cfg.format == SerialFormat::Format4 ? kSerialCrLfSize : 0;
        data = head + dataSize + 1 + sum + crlf;
        ack = head + crlf;
        nak = head + code + crlf;
    }
    return std::max(r.isWrite() ? ack : data, nak);
}

ParseStatus frameSerialTryParse(ByteView buffer, const Request& r, const FrameConfig& cfg,
                                SerialCursor& cursor, size_t& frameLength, Error& error) noexcept {
    SerialFrame frame;
    ParseStatus status = serialFeed(serialParamsOf(cfg), buffer, cursor, frame, error);
    if (status == ParseStatus::NeedMore) {
        return status;
    }
    frameLength = frame.end;
    if (status == ParseStatus::Failed) {
        return status; // serialFeed() set the error (sum check, terminator, size).
    }

    Error routeError = checkRoute(cfg, buffer.data + frame.routeOffset);
    if (routeError.ok() && cfg.format == SerialFormat::Format2) {
        routeError = checkBlock(cfg, buffer.data + frame.blockOffset);
    }
    if (!routeError.ok()) {
        error = routeError;
        return ParseStatus::Failed;
    }

    if (frame.kind == SerialResponseKind::Nak) {
        // The error code is 2 hex characters in 1C (spec E9), 4 in 3C.
        ByteView wireCode{buffer.data + frame.dataOffset, frame.dataSize};
        if (cfg.frame == FrameType::F1C) {
            auto code = AsciiCodec::getU8(wireCode);
            error = code.hasValue() ? plcError(code.value()) : code.error();
        } else {
            auto code = AsciiCodec::getU16(wireCode);
            error = code.hasValue() ? plcError(code.value()) : code.error();
        }
        return ParseStatus::Failed;
    }
    if (frame.kind == SerialResponseKind::Ack) {
        if (!r.isWrite()) {
            error =
                serialProtocolError(ErrorCode::LengthMismatch, "serial read answered without data");
            return ParseStatus::Failed;
        }
        return ParseStatus::Done;
    }
    if (r.isWrite() || frame.dataSize != responseDataSize(r, cfg)) {
        error = serialProtocolError(ErrorCode::LengthMismatch,
                                    "serial response data size does not match the request");
        return ParseStatus::Failed;
    }
    return ParseStatus::Done;
}

Expected<size_t> frameSerialPayload(ByteView buffer, size_t skipped, const Request& r,
                                    const FrameConfig& cfg, MutableByteView out) noexcept {
    const size_t dataOffset = skipped + serialDataOffset(serialParamsOf(cfg));
    ByteView wireData{buffer.data + dataOffset, responseDataSize(r, cfg)};
    return cfg.frame == FrameType::F1C ? a1cResponseData(r, wireData, out)
                                       : qnaResponseData<AsciiCodec>(r, wireData, out);
}

} // namespace mc::detail
