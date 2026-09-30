// src/core/protocol/frame_1e.cpp -- spec section 5.3.
#include "frame_1e.h"

namespace mc::detail {
namespace {

// Response subheader = command code OR 80H (spec 5.3).
constexpr uint8_t kResponseFlag = 0x80;
// End codes (spec 5.3): normal, and the one that is followed by an abnormal code.
constexpr uint8_t kEndNormal = 0x00;
constexpr uint8_t kEndWithAbnormal = 0x5B;

Error frameMismatchError() noexcept {
    Error e{};
    e.category = ErrorCategory::Protocol;
    e.code = ErrorCode::FrameMismatch;
    e.message = "1E subheader mismatch";
    return e;
}

Error plcError(uint8_t endCode) noexcept {
    Error e{};
    e.category = ErrorCategory::Plc;
    e.code = ErrorCode::PlcError;
    e.plcCode = endCode;
    e.message = "PLC returned a non-zero 1E end code";
    return e;
}

} // namespace

template <class Codec>
Expected<size_t> frame1eEncode(const Request& r, const FrameConfig& cfg,
                                MutableByteView out) noexcept {
    size_t needed = frame1eEncodedSize<Codec>(r);
    if (out.size < needed) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }

    size_t offset = 0;
    // Subheader = command code (spec 5.3 field 1); it is not part of the request data.
    (void)Codec::putU8(a1eCommandCode(r), MutableByteView{out.data + offset, Codec::u8Size()});
    offset += Codec::u8Size();
    (void)Codec::putU8(cfg.pc, MutableByteView{out.data + offset, Codec::u8Size()});
    offset += Codec::u8Size();
    (void)Codec::putU16(cfg.monitoringTimer, MutableByteView{out.data + offset, Codec::u16Size()});
    offset += Codec::u16Size();

    auto dataResult =
        a1eRequestData<Codec>(r, MutableByteView{out.data + offset, out.size - offset});
    if (!dataResult.hasValue()) {
        return Expected<size_t>(dataResult.error());
    }
    offset += dataResult.value();

    return Expected<size_t>(offset);
}

template <class Codec>
ParseStatus frame1eTryParse(ByteView buffer, const Request& r, size_t& frameLength,
                             Error& error) noexcept {
    size_t headSize = frame1eResponseHeadSize<Codec>();
    if (buffer.size < headSize) {
        return ParseStatus::NeedMore;
    }

    // A subheader that is not even valid hex is as wrong as one with the wrong value.
    auto subheaderR = Codec::getU8(ByteView{buffer.data, Codec::u8Size()});
    uint8_t expectedSubheader = static_cast<uint8_t>(a1eCommandCode(r) | kResponseFlag);
    if (!subheaderR.hasValue() || subheaderR.value() != expectedSubheader) {
        frameLength = headSize;
        error = frameMismatchError();
        return ParseStatus::Failed;
    }

    auto endCodeR = Codec::getU8(ByteView{buffer.data + Codec::u8Size(), Codec::u8Size()});
    if (!endCodeR.hasValue()) {
        frameLength = headSize;
        error = endCodeR.error();
        return ParseStatus::Failed;
    }
    uint8_t endCode = endCodeR.value();

    if (endCode == kEndNormal) {
        size_t total = headSize + a1eResponseDataSize<Codec>(r);
        if (buffer.size < total) {
            return ParseStatus::NeedMore;
        }
        frameLength = total;
        return ParseStatus::Done;
    }

    if (endCode == kEndWithAbnormal) {
        size_t total = headSize + Codec::u8Size();
        if (buffer.size < total) {
            return ParseStatus::NeedMore;
        }
        frameLength = total;
        auto abnormalR = Codec::getU8(ByteView{buffer.data + headSize, Codec::u8Size()});
        if (!abnormalR.hasValue()) {
            error = abnormalR.error();
            return ParseStatus::Failed;
        }
        Error e = plcError(endCode);
        e.abnormalCode = abnormalR.value();
        error = e;
        return ParseStatus::Failed;
    }

    // Any other end code carries no abnormal code and no data: the frame ends here, and nothing
    // more is awaited (1E-06).
    frameLength = headSize;
    error = plcError(endCode);
    return ParseStatus::Failed;
}

template Expected<size_t> frame1eEncode<BinaryCodec>(const Request&, const FrameConfig&,
                                                      MutableByteView) noexcept;
template Expected<size_t> frame1eEncode<AsciiCodec>(const Request&, const FrameConfig&,
                                                     MutableByteView) noexcept;
template ParseStatus frame1eTryParse<BinaryCodec>(ByteView, const Request&, size_t&,
                                                   Error&) noexcept;
template ParseStatus frame1eTryParse<AsciiCodec>(ByteView, const Request&, size_t&,
                                                  Error&) noexcept;

} // namespace mc::detail
