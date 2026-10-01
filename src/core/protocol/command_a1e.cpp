#include "command_a1e.h"

namespace mc::detail {
namespace {

Error lengthMismatchError() noexcept {
    Error e{};
    e.category = ErrorCategory::Protocol;
    e.code = ErrorCode::LengthMismatch;
    e.message = "1E response data length does not match the request";
    return e;
}

Error pointCountError() noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::PointCount;
    e.message = "a 1E test command takes at most 255 entries";
    return e;
}

// Writes n (u8) + the fixed 00 (u8) at the start of a test command's request data.
template <class Codec> size_t putTestHead(size_t n, MutableByteView out) noexcept {
    (void)Codec::putU8(static_cast<uint8_t>(n), MutableByteView{out.data, Codec::u8Size()});
    (void)Codec::putU8(0, MutableByteView{out.data + Codec::u8Size(), Codec::u8Size()});
    return Codec::u8Size() * 2;
}

} // namespace

template <class Codec>
Expected<size_t> a1eRequestData(const Request& r, MutableByteView out) noexcept {
    size_t needed = a1eRequestDataSize<Codec>(r);
    if (out.size < needed) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }

    size_t offset = 0;
    size_t deviceSize = e1DeviceSize(Codec::kDataCode);
    auto deviceResult =
        e1Device(r.head, Codec::kDataCode, MutableByteView{out.data + offset, deviceSize});
    if (!deviceResult.hasValue()) {
        return Expected<size_t>(deviceResult.error());
    }
    offset += deviceResult.value();

    // Points is a u8; 256 wraps to 00 as the spec requires (E8).
    (void)Codec::putU8(static_cast<uint8_t>(r.count),
                        MutableByteView{out.data + offset, Codec::u8Size()});
    offset += Codec::u8Size();
    (void)Codec::putU8(0, MutableByteView{out.data + offset, Codec::u8Size()});
    offset += Codec::u8Size();

    if (r.isWrite()) {
        MutableByteView writeOut{out.data + offset, out.size - offset};
        auto writeResult = r.isBitOp() ? encodeBits<Codec>(r, writeOut)
                                        : Codec::putWords(r.data, writeOut);
        if (!writeResult.hasValue()) {
            return writeResult;
        }
        offset += writeResult.value();
    }

    return Expected<size_t>(offset);
}

template <class Codec>
Expected<size_t> a1eResponseData(const Request& r, ByteView in,
                                  MutableByteView payloadOut) noexcept {
    if (in.size != a1eResponseDataSize<Codec>(r)) {
        return Expected<size_t>(lengthMismatchError());
    }
    if (r.isWrite()) {
        return Expected<size_t>(size_t{0});
    }
    if (r.isBitOp()) {
        return decodeBits<Codec>(r, in, payloadOut);
    }
    return Codec::getWords(in, r.count, payloadOut);
}

template <class Codec>
Expected<size_t> a1eTestBitsRequestData(const A1eTestBit* items, size_t n,
                                         MutableByteView out) noexcept {
    if (n > 255) {
        return Expected<size_t>(pointCountError());
    }
    if (out.size < a1eTestBitsRequestDataSize<Codec>(n)) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }
    size_t offset = putTestHead<Codec>(n, out);
    size_t deviceSize = e1DeviceSize(Codec::kDataCode);
    for (size_t i = 0; i < n; ++i) {
        auto deviceResult = e1Device(items[i].device, Codec::kDataCode,
                                      MutableByteView{out.data + offset, deviceSize});
        if (!deviceResult.hasValue()) {
            return Expected<size_t>(deviceResult.error());
        }
        offset += deviceResult.value();
        (void)Codec::putU8(items[i].on ? uint8_t{1} : uint8_t{0},
                            MutableByteView{out.data + offset, Codec::u8Size()});
        offset += Codec::u8Size();
    }
    return Expected<size_t>(offset);
}

template <class Codec>
Expected<size_t> a1eTestWordsRequestData(const A1eTestWord* items, size_t n,
                                          MutableByteView out) noexcept {
    if (n > 255) {
        return Expected<size_t>(pointCountError());
    }
    if (out.size < a1eTestWordsRequestDataSize<Codec>(n)) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }
    size_t offset = putTestHead<Codec>(n, out);
    size_t deviceSize = e1DeviceSize(Codec::kDataCode);
    for (size_t i = 0; i < n; ++i) {
        auto deviceResult = e1Device(items[i].device, Codec::kDataCode,
                                      MutableByteView{out.data + offset, deviceSize});
        if (!deviceResult.hasValue()) {
            return Expected<size_t>(deviceResult.error());
        }
        offset += deviceResult.value();
        (void)Codec::putU16(items[i].value, MutableByteView{out.data + offset, Codec::u16Size()});
        offset += Codec::u16Size();
    }
    return Expected<size_t>(offset);
}

// Only two Codec types ever exist (field_codec.h); explicit instantiation keeps the bodies out of
// command_a1e.h, as command_qna.cpp does.
template Expected<size_t> a1eRequestData<AsciiCodec>(const Request&, MutableByteView) noexcept;
template Expected<size_t> a1eRequestData<BinaryCodec>(const Request&, MutableByteView) noexcept;
template Expected<size_t> a1eResponseData<AsciiCodec>(const Request&, ByteView,
                                                        MutableByteView) noexcept;
template Expected<size_t> a1eResponseData<BinaryCodec>(const Request&, ByteView,
                                                         MutableByteView) noexcept;
template Expected<size_t> a1eTestBitsRequestData<AsciiCodec>(const A1eTestBit*, size_t,
                                                               MutableByteView) noexcept;
template Expected<size_t> a1eTestBitsRequestData<BinaryCodec>(const A1eTestBit*, size_t,
                                                                MutableByteView) noexcept;
template Expected<size_t> a1eTestWordsRequestData<AsciiCodec>(const A1eTestWord*, size_t,
                                                                MutableByteView) noexcept;
template Expected<size_t> a1eTestWordsRequestData<BinaryCodec>(const A1eTestWord*, size_t,
                                                                 MutableByteView) noexcept;

} // namespace mc::detail
