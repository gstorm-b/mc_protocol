// src/core/protocol/frame_3e.cpp -- spec section 5.1. Field layout follows the module spec's own
// Code Style example: separate binary/ASCII literal arrays, not one derived from the other.
#include "frame_3e.h"

namespace mc::detail {
namespace {

constexpr uint8_t kSub3eReqBin[] = {0x50, 0x00};
constexpr uint8_t kSub3eReqAsc[] = {'5', '0', '0', '0'};
constexpr uint8_t kSub3eRespBin[] = {0xD0, 0x00};
constexpr uint8_t kSub3eRespAsc[] = {'D', '0', '0', '0'};

Error frameMismatchError() noexcept {
    Error e{};
    e.category = ErrorCategory::Protocol;
    e.code = ErrorCode::FrameMismatch;
    e.message = "3E subheader or route mismatch";
    return e;
}

Error lengthMismatchError() noexcept {
    Error e{};
    e.category = ErrorCategory::Protocol;
    e.code = ErrorCode::LengthMismatch;
    e.message = "3E response data length mismatch";
    return e;
}

Error plcError(uint16_t endCode) noexcept {
    Error e{};
    e.category = ErrorCategory::Plc;
    e.code = ErrorCode::PlcError;
    e.plcCode = endCode;
    e.message = "PLC returned a non-zero 3E end code";
    return e;
}

} // namespace

template <class Codec>
Expected<size_t> frame3eEncode(const Request& r, const FrameConfig& cfg,
                                MutableByteView out) noexcept {
    size_t needed = frame3eEncodedSize<Codec>(r, cfg.series);
    if (out.size < needed) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }

    size_t offset = 0;
    (void)Codec::putFixed(ByteView{kSub3eReqBin, sizeof(kSub3eReqBin)},
                           ByteView{kSub3eReqAsc, sizeof(kSub3eReqAsc)},
                           MutableByteView{out.data + offset, Codec::u16Size()});
    offset += Codec::u16Size();

    (void)Codec::putU8(cfg.network, MutableByteView{out.data + offset, Codec::u8Size()});
    offset += Codec::u8Size();
    (void)Codec::putU8(cfg.pc, MutableByteView{out.data + offset, Codec::u8Size()});
    offset += Codec::u8Size();
    (void)Codec::putU16(cfg.io, MutableByteView{out.data + offset, Codec::u16Size()});
    offset += Codec::u16Size();
    (void)Codec::putU8(cfg.station, MutableByteView{out.data + offset, Codec::u8Size()});
    offset += Codec::u8Size();

    size_t reqDataSize = qnaRequestDataSize<Codec>(r, cfg.series);
    uint16_t lengthValue = static_cast<uint16_t>(Codec::u16Size() + reqDataSize);
    (void)Codec::putU16(lengthValue, MutableByteView{out.data + offset, Codec::u16Size()});
    offset += Codec::u16Size();

    (void)Codec::putU16(cfg.monitoringTimer, MutableByteView{out.data + offset, Codec::u16Size()});
    offset += Codec::u16Size();

    auto reqResult =
        qnaRequestData<Codec>(r, cfg.series, MutableByteView{out.data + offset, out.size - offset});
    if (!reqResult.hasValue()) {
        return Expected<size_t>(reqResult.error());
    }
    offset += reqResult.value();

    return Expected<size_t>(offset);
}

template <class Codec>
ParseStatus frame3eTryParse(ByteView buffer, const Request& r, const FrameConfig& cfg,
                             size_t& frameLength, Error& error) noexcept {
    size_t headerSize = frame3eHeaderSize<Codec>();
    if (buffer.size < headerSize) {
        return ParseStatus::NeedMore;
    }

    if (!Codec::matchesFixed(ByteView{buffer.data, Codec::u16Size()},
                              ByteView{kSub3eRespBin, sizeof(kSub3eRespBin)},
                              ByteView{kSub3eRespAsc, sizeof(kSub3eRespAsc)})) {
        frameLength = headerSize;
        error = frameMismatchError();
        return ParseStatus::Failed;
    }

    size_t offset = Codec::u16Size();
    auto networkR = Codec::getU8(ByteView{buffer.data + offset, Codec::u8Size()});
    offset += Codec::u8Size();
    auto pcR = Codec::getU8(ByteView{buffer.data + offset, Codec::u8Size()});
    offset += Codec::u8Size();
    auto ioR = Codec::getU16(ByteView{buffer.data + offset, Codec::u16Size()});
    offset += Codec::u16Size();
    auto stationR = Codec::getU8(ByteView{buffer.data + offset, Codec::u8Size()});
    offset += Codec::u8Size();
    // Binary get* never fails; ASCII get* can (invalid hex character). Checked uniformly so this
    // already-templated function is correct once a later task instantiates it for AsciiCodec.
    if (!networkR.hasValue() || !pcR.hasValue() || !ioR.hasValue() || !stationR.hasValue()) {
        frameLength = headerSize;
        error = !networkR.hasValue()  ? networkR.error()
                : !pcR.hasValue()     ? pcR.error()
                : !ioR.hasValue()     ? ioR.error()
                                      : stationR.error();
        return ParseStatus::Failed;
    }

    if (cfg.checkRoute) {
        bool routeMatches = networkR.value() == cfg.network && pcR.value() == cfg.pc &&
                             ioR.value() == cfg.io && stationR.value() == cfg.station;
        if (!routeMatches) {
            frameLength = headerSize;
            error = frameMismatchError();
            return ParseStatus::Failed;
        }
    }

    auto lengthR = Codec::getU16(ByteView{buffer.data + offset, Codec::u16Size()});
    offset += Codec::u16Size(); // offset == headerSize from here on.
    if (!lengthR.hasValue()) {
        frameLength = headerSize;
        error = lengthR.error();
        return ParseStatus::Failed;
    }
    size_t l = lengthR.value();

    if (buffer.size < headerSize + l) {
        return ParseStatus::NeedMore;
    }
    frameLength = headerSize + l; // known for every outcome from here on, success or failure.

    size_t endCodeSize = Codec::u16Size();
    if (l < endCodeSize) {
        error = lengthMismatchError();
        return ParseStatus::Failed;
    }

    auto endCodeR = Codec::getU16(ByteView{buffer.data + offset, endCodeSize});
    if (!endCodeR.hasValue()) {
        error = endCodeR.error();
        return ParseStatus::Failed;
    }
    uint16_t endCode = endCodeR.value();
    size_t remaining = l - endCodeSize;

    if (endCode == 0) {
        size_t expectedWireSize = qnaResponseDataSize<Codec>(r);
        if (remaining != expectedWireSize) {
            error = lengthMismatchError();
            return ParseStatus::Failed;
        }
        return ParseStatus::Done;
    }

    Error e = plcError(endCode);
    size_t errorInfoSize = frame3eErrorInfoSize<Codec>();
    if (remaining >= errorInfoSize) {
        size_t infoOffset = offset + endCodeSize;
        auto infoNetworkR = Codec::getU8(ByteView{buffer.data + infoOffset, Codec::u8Size()});
        infoOffset += Codec::u8Size();
        auto infoPcR = Codec::getU8(ByteView{buffer.data + infoOffset, Codec::u8Size()});
        infoOffset += Codec::u8Size();
        auto infoIoR = Codec::getU16(ByteView{buffer.data + infoOffset, Codec::u16Size()});
        infoOffset += Codec::u16Size();
        auto infoStationR = Codec::getU8(ByteView{buffer.data + infoOffset, Codec::u8Size()});
        infoOffset += Codec::u8Size();
        auto infoCommandR = Codec::getU16(ByteView{buffer.data + infoOffset, Codec::u16Size()});
        infoOffset += Codec::u16Size();
        auto infoSubcommandR = Codec::getU16(ByteView{buffer.data + infoOffset, Codec::u16Size()});
        // A malformed error-information block (ASCII only; T-018) does not invalidate the PLC
        // error itself -- e.info simply stays zero, same as when remaining < errorInfoSize.
        if (infoNetworkR.hasValue() && infoPcR.hasValue() && infoIoR.hasValue() &&
            infoStationR.hasValue() && infoCommandR.hasValue() && infoSubcommandR.hasValue()) {
            e.info.network = infoNetworkR.value();
            e.info.pc = infoPcR.value();
            e.info.io = infoIoR.value();
            e.info.station = infoStationR.value();
            e.info.command = infoCommandR.value();
            e.info.subcommand = infoSubcommandR.value();
        }
    }
    error = e;
    return ParseStatus::Failed;
}

// Both codecs (T-018 adds AsciiCodec; T-017 had only instantiated BinaryCodec, with a link
// error, not a runtime one, standing behind protocol.cpp's own DataCode dispatch in the
// meantime).
template Expected<size_t> frame3eEncode<BinaryCodec>(const Request&, const FrameConfig&,
                                                      MutableByteView) noexcept;
template Expected<size_t> frame3eEncode<AsciiCodec>(const Request&, const FrameConfig&,
                                                     MutableByteView) noexcept;
template ParseStatus frame3eTryParse<BinaryCodec>(ByteView, const Request&, const FrameConfig&,
                                                   size_t&, Error&) noexcept;
template ParseStatus frame3eTryParse<AsciiCodec>(ByteView, const Request&, const FrameConfig&,
                                                  size_t&, Error&) noexcept;

} // namespace mc::detail
