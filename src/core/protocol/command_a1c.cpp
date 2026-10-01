#include "command_a1c.h"

namespace mc::detail {
namespace {

Error lengthMismatchError() noexcept {
    Error e{};
    e.category = ErrorCategory::Protocol;
    e.code = ErrorCode::LengthMismatch;
    e.message = "1C response data length does not match the request";
    return e;
}

Error invalidMessageWaitError() noexcept {
    Error e{};
    e.category = ErrorCategory::Config;
    e.code = ErrorCode::InvalidConfig;
    e.message = "messageWait must be 0-15";
    return e;
}

Error pointCountError() noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::PointCount;
    e.message = "a 1C test command takes at most 255 entries";
    return e;
}

// Writes the command letters and the message wait (one upper-case hex digit) at `out` and returns
// the characters written; the caller has checked the size and `messageWait <= 15`.
size_t putHead(const A1cCommand& command, uint8_t messageWait, MutableByteView out) noexcept {
    out.data[0] = static_cast<uint8_t>(command.letters[0]);
    out.data[1] = static_cast<uint8_t>(command.letters[1]);
    out.data[2] =
        static_cast<uint8_t>(messageWait < 10 ? '0' + messageWait : 'A' + messageWait - 10);
    return kA1cCommandSize + kA1cMessageWaitSize;
}

// Writes n (2 hex characters) behind the head of a test command.
size_t putTestCount(size_t n, MutableByteView out) noexcept {
    (void)AsciiCodec::putU8(static_cast<uint8_t>(n),
                            MutableByteView{out.data, AsciiCodec::u8Size()});
    return AsciiCodec::u8Size();
}

} // namespace

Expected<size_t> a1cRequestData(const Request& r, C1CommandSet commandSet, uint8_t messageWait,
                                MutableByteView out) noexcept {
    if (messageWait > 15) {
        return Expected<size_t>(invalidMessageWaitError());
    }
    if (out.size < a1cRequestDataSize(r, commandSet)) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }

    size_t offset = putHead(a1cCommand(r, commandSet), messageWait, out);

    size_t deviceSize = c1DeviceSize(r.head, commandSet);
    auto deviceResult =
        c1Device(r.head, commandSet, MutableByteView{out.data + offset, deviceSize});
    if (!deviceResult.hasValue()) {
        return Expected<size_t>(deviceResult.error());
    }
    offset += deviceResult.value();

    // Points is 2 hex characters; 256 wraps to 00 as the spec requires (E8).
    (void)AsciiCodec::putU8(static_cast<uint8_t>(r.count),
                            MutableByteView{out.data + offset, AsciiCodec::u8Size()});
    offset += AsciiCodec::u8Size();

    if (r.isWrite()) {
        MutableByteView writeOut{out.data + offset, out.size - offset};
        auto writeResult = r.isBitOp() ? encodeBits<AsciiCodec>(r, writeOut)
                                       : AsciiCodec::putWords(r.data, writeOut);
        if (!writeResult.hasValue()) {
            return writeResult;
        }
        offset += writeResult.value();
    }
    return Expected<size_t>(offset);
}

Expected<size_t> a1cResponseData(const Request& r, ByteView in,
                                 MutableByteView payloadOut) noexcept {
    if (in.size != a1cResponseDataSize(r)) {
        return Expected<size_t>(lengthMismatchError());
    }
    if (r.isWrite()) {
        return Expected<size_t>(size_t{0});
    }
    if (r.isBitOp()) {
        return decodeBits<AsciiCodec>(r, in, payloadOut);
    }
    return AsciiCodec::getWords(in, r.count, payloadOut);
}

size_t a1cTestBitsRequestDataSize(const A1cTestBit* items, size_t n,
                                  C1CommandSet commandSet) noexcept {
    size_t size = kA1cCommandSize + kA1cMessageWaitSize + AsciiCodec::u8Size();
    for (size_t i = 0; i < n; ++i) {
        size += c1DeviceSize(items[i].device, commandSet) + 1;
    }
    return size;
}

size_t a1cTestWordsRequestDataSize(const A1cTestWord* items, size_t n,
                                   C1CommandSet commandSet) noexcept {
    size_t size = kA1cCommandSize + kA1cMessageWaitSize + AsciiCodec::u8Size();
    for (size_t i = 0; i < n; ++i) {
        size += c1DeviceSize(items[i].device, commandSet) + AsciiCodec::u16Size();
    }
    return size;
}

Expected<size_t> a1cTestBitsRequestData(const A1cTestBit* items, size_t n, C1CommandSet commandSet,
                                        uint8_t messageWait, MutableByteView out) noexcept {
    if (n > 255) {
        return Expected<size_t>(pointCountError());
    }
    if (messageWait > 15) {
        return Expected<size_t>(invalidMessageWaitError());
    }
    if (out.size < a1cTestBitsRequestDataSize(items, n, commandSet)) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }
    const bool ana = commandSet == C1CommandSet::AnA;
    size_t offset = putHead(A1cCommand{{ana ? 'J' : 'B', 'T'}}, messageWait, out);
    offset += putTestCount(n, MutableByteView{out.data + offset, out.size - offset});
    for (size_t i = 0; i < n; ++i) {
        size_t deviceSize = c1DeviceSize(items[i].device, commandSet);
        auto deviceResult =
            c1Device(items[i].device, commandSet, MutableByteView{out.data + offset, deviceSize});
        if (!deviceResult.hasValue()) {
            return Expected<size_t>(deviceResult.error());
        }
        offset += deviceResult.value();
        out.data[offset++] = static_cast<uint8_t>(items[i].on ? '1' : '0');
    }
    return Expected<size_t>(offset);
}

Expected<size_t> a1cTestWordsRequestData(const A1cTestWord* items, size_t n,
                                         C1CommandSet commandSet, uint8_t messageWait,
                                         MutableByteView out) noexcept {
    if (n > 255) {
        return Expected<size_t>(pointCountError());
    }
    if (messageWait > 15) {
        return Expected<size_t>(invalidMessageWaitError());
    }
    if (out.size < a1cTestWordsRequestDataSize(items, n, commandSet)) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }
    const bool ana = commandSet == C1CommandSet::AnA;
    size_t offset = putHead(A1cCommand{{ana ? 'Q' : 'W', 'T'}}, messageWait, out);
    offset += putTestCount(n, MutableByteView{out.data + offset, out.size - offset});
    for (size_t i = 0; i < n; ++i) {
        size_t deviceSize = c1DeviceSize(items[i].device, commandSet);
        auto deviceResult =
            c1Device(items[i].device, commandSet, MutableByteView{out.data + offset, deviceSize});
        if (!deviceResult.hasValue()) {
            return Expected<size_t>(deviceResult.error());
        }
        offset += deviceResult.value();
        (void)AsciiCodec::putU16(items[i].value,
                                 MutableByteView{out.data + offset, AsciiCodec::u16Size()});
        offset += AsciiCodec::u16Size();
    }
    return Expected<size_t>(offset);
}

} // namespace mc::detail
