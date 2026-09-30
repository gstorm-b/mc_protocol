#include "command_qna.h"

#include "bit_payload.h"

namespace mc::detail {
namespace {

Error lengthMismatchError() noexcept {
    Error e{};
    e.category = ErrorCategory::Protocol;
    e.code = ErrorCode::LengthMismatch;
    e.message = "response data length does not match the request";
    return e;
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
