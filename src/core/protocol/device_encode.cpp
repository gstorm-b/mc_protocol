#include "device_encode.h"

#include "field_codec.h"

namespace mc::detail {
namespace {

Error invalidDeviceError() noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::InvalidDevice;
    e.message = "device has no code for this frame family";
    return e;
}

// The six Timer/Counter symbols (TS, TC, TN, CS, CC, CN) are the only devices with a 1C code
// longer than one character (spec §3.3's "T/C" column); mirrors validate.cpp's own helper of the
// same name (core-model), duplicated here rather than shared across modules for a two-line check.
bool isTimerOrCounterDevice(const DeviceInfo& info) noexcept {
    return info.c1Code[0] != '\0' && info.c1Code[1] != '\0';
}

// Writes `number` as exactly `width` ASCII digits in `radix`, zero-padded, upper-case for hex
// (spec §3.2: "*" and leading zeros are never a space). Precondition: `number` fits in `width`
// digits of `radix` and `out.size >= width` -- validate() (core-model) already guarantees the
// first before any codec runs (field_codec.h's own note on field overflow applies here too); the
// second is checked by this file's own callers before this is reached.
void putFixedWidthNumber(uint32_t number, Radix radix, size_t width, MutableByteView out) noexcept {
    uint32_t base = radix == Radix::Hex ? 16u : 10u;
    for (size_t i = 0; i < width; ++i) {
        uint32_t digit = number % base;
        number /= base;
        char c = digit < 10 ? static_cast<char>('0' + digit) : static_cast<char>('A' + digit - 10);
        out.data[width - 1 - i] = static_cast<uint8_t>(c);
    }
}

} // namespace

Expected<size_t> qnaDevice(const Device& d, DataCode code, PlcSeries series,
                            MutableByteView out) noexcept {
    const DeviceInfo& info = deviceInfo(d.type);
    size_t needed = qnaDeviceSize(code, series);
    if (out.size < needed) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }

    if (code == DataCode::Ascii) {
        if (series == PlcSeries::QL) {
            if (info.qnaAsciiQL[0] == '\0') {
                return Expected<size_t>(invalidDeviceError());
            }
            out.data[0] = static_cast<uint8_t>(info.qnaAsciiQL[0]);
            out.data[1] = static_cast<uint8_t>(info.qnaAsciiQL[1]);
            putFixedWidthNumber(d.number, info.radix, 6, MutableByteView{out.data + 2, 6});
        } else {
            if (info.qnaAsciiIqr[0] == '\0') {
                return Expected<size_t>(invalidDeviceError());
            }
            for (size_t i = 0; i < 4; ++i) {
                out.data[i] = static_cast<uint8_t>(info.qnaAsciiIqr[i]);
            }
            putFixedWidthNumber(d.number, info.radix, 8, MutableByteView{out.data + 4, 8});
        }
    } else {
        if (series == PlcSeries::QL) {
            if (info.qnaBinQL == kNoCode) {
                return Expected<size_t>(invalidDeviceError());
            }
            // Number LE3 (spec §3.3): no field_codec.h primitive is 3 bytes wide, so this one
            // field is written directly rather than through a borrowed 4-byte helper.
            out.data[0] = static_cast<uint8_t>(d.number & 0xFFu);
            out.data[1] = static_cast<uint8_t>((d.number >> 8) & 0xFFu);
            out.data[2] = static_cast<uint8_t>((d.number >> 16) & 0xFFu);
            out.data[3] = static_cast<uint8_t>(info.qnaBinQL & 0xFFu);
        } else {
            if (info.qnaBinIqr == kNoCode) {
                return Expected<size_t>(invalidDeviceError());
            }
            (void)BinaryCodec::putU32(d.number, MutableByteView{out.data, 4});
            (void)BinaryCodec::putU16(info.qnaBinIqr, MutableByteView{out.data + 4, 2});
        }
    }
    return Expected<size_t>(needed);
}

Expected<size_t> e1Device(const Device& d, DataCode code, MutableByteView out) noexcept {
    const DeviceInfo& info = deviceInfo(d.type);
    size_t needed = e1DeviceSize(code);
    if (out.size < needed) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }
    if (info.e1Code == kNoCode) {
        return Expected<size_t>(invalidDeviceError());
    }

    if (code == DataCode::Ascii) {
        (void)AsciiCodec::putU16(info.e1Code, MutableByteView{out.data, 4});
        (void)AsciiCodec::putU32(d.number, MutableByteView{out.data + 4, 8});
    } else {
        (void)BinaryCodec::putU32(d.number, MutableByteView{out.data, 4});
        (void)BinaryCodec::putU16(info.e1Code, MutableByteView{out.data + 4, 2});
    }
    return Expected<size_t>(needed);
}

size_t c1DeviceSize(const Device& d, C1CommandSet commandSet) noexcept {
    const DeviceInfo& info = deviceInfo(d.type);
    bool timerOrCounter = isTimerOrCounterDevice(info);
    size_t codeWidth = timerOrCounter ? 2 : 1;
    size_t numberWidth;
    if (commandSet == C1CommandSet::ACPU) {
        numberWidth = timerOrCounter ? 3 : 4;
    } else {
        numberWidth = timerOrCounter ? 5 : 6;
    }
    return codeWidth + numberWidth;
}

Expected<size_t> c1Device(const Device& d, C1CommandSet commandSet, MutableByteView out) noexcept {
    const DeviceInfo& info = deviceInfo(d.type);
    if (info.c1Code[0] == '\0') {
        return Expected<size_t>(invalidDeviceError());
    }
    size_t needed = c1DeviceSize(d, commandSet);
    if (out.size < needed) {
        return Expected<size_t>(fieldBufferTooSmallError());
    }

    size_t codeWidth = isTimerOrCounterDevice(info) ? 2 : 1;
    size_t numberWidth = needed - codeWidth;
    for (size_t i = 0; i < codeWidth; ++i) {
        out.data[i] = static_cast<uint8_t>(info.c1Code[i]);
    }
    putFixedWidthNumber(d.number, info.radix, numberWidth,
                        MutableByteView{out.data + codeWidth, numberWidth});
    return Expected<size_t>(needed);
}

} // namespace mc::detail
