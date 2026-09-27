#include "command_qna.h"

#include <algorithm>

namespace mc::detail {
namespace {

Error lengthMismatchError() noexcept {
    Error e{};
    e.category = ErrorCategory::Protocol;
    e.code = ErrorCode::LengthMismatch;
    e.message = "response data length does not match the request";
    return e;
}

// Encodes r.data (write payload, r.bitLayout) as `count` bits of wire data into `out`, in chunks
// of up to 8 points: field_codec.h's Codec::putBits() always treats index 0 of a call as the
// high nibble of its first byte (Binary) / its first character (Ascii), so a chunk boundary must
// itself be nibble-aligned. Every chunk here is a full 8 points except possibly the last, so
// `pointsDone` is always even where it matters (Binary); Ascii has no alignment constraint at
// all. No allocation: `chunk` is a fixed 8-byte stack array, not sized by `count`.
template <class Codec>
Expected<size_t> encodeBits(const Request& r, MutableByteView out) noexcept {
    size_t count = r.count;
    if (r.bitLayout == BitLayout::BytePerPoint) {
        return Codec::putBits(r.data, out);
    }

    size_t pointsDone = 0;
    while (pointsDone < count) {
        size_t chunkCount = std::min<size_t>(8, count - pointsDone);
        uint8_t chunk[8] = {};
        uint8_t packedByte = r.data.data[pointsDone / 8];
        for (size_t b = 0; b < chunkCount; ++b) {
            chunk[b] = static_cast<uint8_t>((packedByte >> b) & 1u);
        }
        size_t offset = Codec::bitsSize(pointsDone);
        auto putResult = Codec::putBits(ByteView{chunk, chunkCount},
                                         MutableByteView{out.data + offset, out.size - offset});
        if (!putResult.hasValue()) {
            return putResult;
        }
        pointsDone += chunkCount;
    }
    return Expected<size_t>(Codec::bitsSize(count));
}

// Inverse of encodeBits(): decodes `count` bits of wire data `in` into payloadOut, in r.bitLayout
// (same chunking rationale as encodeBits()).
template <class Codec>
Expected<size_t> decodeBits(const Request& r, ByteView in, MutableByteView payloadOut) noexcept {
    size_t count = r.count;
    if (r.bitLayout == BitLayout::BytePerPoint) {
        return Codec::getBits(in, count, payloadOut);
    }

    size_t packedSize = (count + 7) / 8;
    for (size_t i = 0; i < packedSize; ++i) {
        payloadOut.data[i] = 0;
    }
    size_t pointsDone = 0;
    while (pointsDone < count) {
        size_t chunkCount = std::min<size_t>(8, count - pointsDone);
        uint8_t chunk[8] = {};
        size_t offset = Codec::bitsSize(pointsDone);
        auto getResult = Codec::getBits(ByteView{in.data + offset, in.size - offset}, chunkCount,
                                         MutableByteView{chunk, chunkCount});
        if (!getResult.hasValue()) {
            return getResult;
        }
        for (size_t b = 0; b < chunkCount; ++b) {
            if (chunk[b] != 0) {
                payloadOut.data[pointsDone / 8] |= static_cast<uint8_t>(1u << b);
            }
        }
        pointsDone += chunkCount;
    }
    return Expected<size_t>(packedSize);
}

} // namespace

template <class Codec>
Expected<size_t> qnaRequestData(const Request& r, PlcSeries series, MutableByteView out) noexcept {
    size_t needed = qnaRequestDataSize<Codec>(r, series);
    if (out.size < needed) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }

    size_t offset = 0;
    (void)Codec::putU16(qnaCommandCode(r.isWrite()),
                         MutableByteView{out.data + offset, Codec::u16Size()});
    offset += Codec::u16Size();

    (void)Codec::putU16(qnaSubcommand(r.isBitOp(), series),
                         MutableByteView{out.data + offset, Codec::u16Size()});
    offset += Codec::u16Size();

    size_t deviceSize = qnaDeviceSize(Codec::kDataCode, series);
    auto deviceResult = qnaDevice(r.head, Codec::kDataCode, series,
                                  MutableByteView{out.data + offset, deviceSize});
    if (!deviceResult.hasValue()) {
        return Expected<size_t>(deviceResult.error());
    }
    offset += deviceResult.value();

    (void)Codec::putU16(r.count, MutableByteView{out.data + offset, Codec::u16Size()});
    offset += Codec::u16Size();

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
Expected<size_t> qnaResponseData(const Request& r, ByteView in,
                                  MutableByteView payloadOut) noexcept {
    size_t expectedSize = qnaResponseDataSize<Codec>(r);
    if (in.size != expectedSize) {
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

// Only two Codec types ever exist (field_codec.h); explicit instantiation keeps qnaRequestData's
// and qnaResponseData's bodies out of command_qna.h (they are declared, not defined, there),
// matching the module spec's Project Structure (this file has its own .cpp, unlike header-only
// field_codec.h). qnaRequestDataSize()/qnaResponseDataSize() are fully defined in the header
// already (plain constexpr-friendly arithmetic) and need no instantiation here.
template Expected<size_t> qnaRequestData<AsciiCodec>(const Request&, PlcSeries,
                                                      MutableByteView) noexcept;
template Expected<size_t> qnaRequestData<BinaryCodec>(const Request&, PlcSeries,
                                                       MutableByteView) noexcept;
template Expected<size_t> qnaResponseData<AsciiCodec>(const Request&, ByteView,
                                                        MutableByteView) noexcept;
template Expected<size_t> qnaResponseData<BinaryCodec>(const Request&, ByteView,
                                                         MutableByteView) noexcept;

} // namespace mc::detail
