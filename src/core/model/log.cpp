// NullLogSink and hexDump() (spec §8.6). hexDump is snprintf-like (formatDevice, T-007): it
// always returns the full logical length, and writes only as many characters as capacity allows.
#include "mc/core/log.h"

namespace mc {

bool NullLogSink::enabled(LogLevel) const noexcept { return false; }

void NullLogSink::write(LogLevel, std::string_view, std::string_view) noexcept {}

namespace {

struct ControlName {
    uint8_t code;
    const char* name;
};

// The eight control codes spec §8.6 names; every other byte (including EOT/CL of spec §2.7,
// which §8.6 does not name) renders as plain hex regardless of `names`.
constexpr ControlName kControlNames[] = {
    {0x02, "<STX>"}, {0x03, "<ETX>"}, {0x05, "<ENQ>"}, {0x06, "<ACK>"},
    {0x15, "<NAK>"}, {0x0D, "<CR>"},  {0x0A, "<LF>"},  {0x10, "<DLE>"},
};

const char* controlNameFor(uint8_t byte) noexcept {
    for (const ControlName& entry : kControlNames) {
        if (entry.code == byte) {
            return entry.name;
        }
    }
    return nullptr;
}

// Appends one character to `out` if `written` is still within `maxWritable`, and always advances
// `written` (the running logical length, which may end up exceeding maxWritable).
void appendChar(char* out, size_t maxWritable, size_t& written, char c) noexcept {
    if (written < maxWritable) {
        out[written] = c;
    }
    ++written;
}

} // namespace

size_t hexDump(ByteView bytes, char* out, size_t capacity, bool names) noexcept {
    static constexpr char kHexDigits[] = "0123456789ABCDEF";
    size_t maxWritable = (capacity > 0) ? capacity - 1 : 0;
    size_t written = 0;

    for (size_t i = 0; i < bytes.size; ++i) {
        if (i > 0) {
            appendChar(out, maxWritable, written, ' ');
        }

        uint8_t b = bytes.data[i];
        const char* name = names ? controlNameFor(b) : nullptr;
        if (name != nullptr) {
            for (const char* p = name; *p != '\0'; ++p) {
                appendChar(out, maxWritable, written, *p);
            }
        } else {
            appendChar(out, maxWritable, written, kHexDigits[(b >> 4) & 0x0Fu]);
            appendChar(out, maxWritable, written, kHexDigits[b & 0x0Fu]);
        }
    }

    if (capacity > 0) {
        out[(written < maxWritable) ? written : maxWritable] = '\0';
    }
    return written;
}

} // namespace mc
