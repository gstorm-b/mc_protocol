// parseDevice() and formatDevice() (spec §3.4): device text <-> Device, independent of any
// frame family. Field width and access-restriction checks are validate()'s job (core-request,
// T-008), not this module's.
#include "mc/core/device.h"

#include "core/model/validate_internal.h"

#include <cstddef>

namespace mc {
namespace {

constexpr size_t kSymbolCount = static_cast<size_t>(DeviceType::Count);
constexpr int kMaxSymbolLen = 3; // "STS", "STC", "STN" are the longest symbols in the table.

char toUpperAscii(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// The value of a digit in any base up to 16, or kInvalidDigit.
constexpr uint32_t kInvalidDigit = 0xFFFFFFFFu;

uint32_t digitValue(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return static_cast<uint32_t>(c - '0');
    }
    char u = toUpperAscii(c);
    if (u >= 'A' && u <= 'F') {
        return static_cast<uint32_t>(u - 'A' + 10);
    }
    return kInvalidDigit;
}

// Compares the first `len` characters of `text` against `symbol` (which is exactly `len`
// characters long, NUL-terminated), case-insensitively.
bool matchesSymbol(std::string_view text, size_t len, const char* symbol) noexcept {
    for (size_t i = 0; i < len; ++i) {
        if (toUpperAscii(text[i]) != symbol[i]) {
            return false;
        }
    }
    return true;
}

size_t symbolLength(const char* symbol) noexcept {
    size_t len = 0;
    while (symbol[len] != '\0') {
        ++len;
    }
    return len;
}

Error invalidDeviceError() noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::InvalidDevice;
    e.message = "invalid device text";
    return e;
}

} // namespace

Expected<Device> parseDevice(std::string_view text) noexcept {
    return parseDevice(text, XyNumbering::Hex);
}

Expected<Device> parseDevice(std::string_view text, XyNumbering xy) noexcept {
    // Longest symbol first (spec §3.4 item 1): try len = 3, 2, 1 in turn; the first table row
    // whose symbol equals that prefix, case-insensitively, wins. Symbols in the table are
    // pairwise distinct within each length, so at most one row can match at a given length.
    for (int len = kMaxSymbolLen; len >= 1; --len) {
        auto symLen = static_cast<size_t>(len);
        if (text.size() <= symLen) {
            continue; // no room left for at least one digit after the symbol
        }
        for (size_t i = 0; i < kSymbolCount; ++i) {
            const DeviceInfo& info = deviceInfo(static_cast<DeviceType>(i));
            if (symbolLength(info.symbol) != symLen) {
                continue;
            }
            if (!matchesSymbol(text, symLen, info.symbol)) {
                continue;
            }

            std::string_view digits = text.substr(symLen);
            const uint32_t base = detail::numberBase(info, xy);
            uint32_t number = 0;
            for (char c : digits) {
                const uint32_t v = digitValue(c);
                if (v >= base) { // also true for kInvalidDigit
                    return Expected<Device>(invalidDeviceError());
                }
                if (number > (0xFFFFFFFFu - v) / base) { // number * base + v would not fit 32 bits
                    return Expected<Device>(invalidDeviceError());
                }
                number = number * base + v;
            }

            Device d{};
            d.type = info.type;
            d.number = number;
            return Expected<Device>(d);
        }
    }
    return Expected<Device>(invalidDeviceError());
}

size_t formatDevice(const Device& d, char* out, size_t capacity) noexcept {
    return formatDevice(d, out, capacity, XyNumbering::Hex);
}

size_t formatDevice(const Device& d, char* out, size_t capacity, XyNumbering xy) noexcept {
    const DeviceInfo& info = deviceInfo(d.type);

    // Collect digits least-significant-first; uint32_t needs at most 11 octal, 10 decimal or 8
    // hex digits.
    char digits[11];
    size_t digitCount = 0;
    uint32_t n = d.number;
    if (n == 0) {
        digits[digitCount++] = '0';
    } else {
        const uint32_t base = detail::numberBase(info, xy);
        while (n > 0) {
            uint32_t digit = n % base;
            digits[digitCount++] =
                (digit < 10) ? static_cast<char>('0' + digit) : static_cast<char>('A' + digit - 10);
            n /= base;
        }
    }

    size_t symLen = symbolLength(info.symbol);
    size_t total = symLen + digitCount;

    if (capacity > 0) {
        size_t maxWritable = capacity - 1;
        size_t written = 0;
        for (size_t i = 0; i < symLen && written < maxWritable; ++i, ++written) {
            out[written] = info.symbol[i];
        }
        for (size_t i = 0; i < digitCount && written < maxWritable; ++i, ++written) {
            out[written] = digits[digitCount - 1 - i];
        }
        out[written] = '\0';
    }
    return total;
}

} // namespace mc
