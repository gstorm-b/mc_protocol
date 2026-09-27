#include "hexascii.h"

namespace mc::detail {
namespace {

Error bufferTooSmallError() noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::BufferTooSmall;
    e.message = "output buffer too small";
    return e;
}

Error invalidCharacterError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Protocol;
    e.code = ErrorCode::InvalidCharacter;
    e.message = message;
    return e;
}

char toUpperHexDigit(uint8_t nibble) noexcept {
    return nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('A' + nibble - 10);
}

// -1 signals "not a hex digit", the trigger for InvalidCharacter (PRIM-06).
int hexDigitValue(uint8_t c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a'); // PRIM-05: lower case accepted on decode.
    }
    return -1;
}

} // namespace

Expected<size_t> hexEncode(ByteView bytes, MutableByteView out) noexcept {
    size_t needed = hexEncodedSize(bytes.size);
    if (out.size < needed) {
        return Expected<size_t>(bufferTooSmallError());
    }
    for (size_t i = 0; i < bytes.size; ++i) {
        uint8_t b = bytes.data[i];
        out.data[2 * i] = static_cast<uint8_t>(toUpperHexDigit(static_cast<uint8_t>(b >> 4)));
        out.data[2 * i + 1] = static_cast<uint8_t>(toUpperHexDigit(static_cast<uint8_t>(b & 0x0F)));
    }
    return Expected<size_t>(needed);
}

Expected<size_t> hexDecode(ByteView text, MutableByteView out) noexcept {
    if (text.size % 2 != 0) {
        return Expected<size_t>(invalidCharacterError("odd number of hex characters"));
    }
    size_t needed = text.size / 2;
    if (out.size < needed) {
        return Expected<size_t>(bufferTooSmallError());
    }
    for (size_t i = 0; i < needed; ++i) {
        int hi = hexDigitValue(text.data[2 * i]);
        int lo = hexDigitValue(text.data[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return Expected<size_t>(invalidCharacterError("non-hex character"));
        }
        out.data[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return Expected<size_t>(needed);
}

} // namespace mc::detail
