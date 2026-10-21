// Batch read and write of a decoded request on the memory image, with the fault and limit lookups
// that turn a request into a PLC error. One routine serves every frame family: they differ only in
// the error codes and in the size a response may have.
#include "mock/mock_internal.h"

#include "core/protocol/field_codec.h"

#include <cstddef>

namespace mc::detail::mock {

namespace {

Outcome failure(uint16_t code, uint8_t abnormal = 0) {
    Outcome o;
    o.ok = false;
    o.plcCode = code;
    o.abnormal = abnormal;
    return o;
}

bool isBitOp(Op op) { return op == Op::ReadBits || op == Op::WriteBits; }

bool isWrite(Op op) { return op == Op::WriteBits || op == Op::WriteWords; }

// Bytes of response data on the wire, for the check that the 3E length field can carry them.
size_t wireDataSize(DataCode code, Op op, uint16_t count) {
    if (code == DataCode::Ascii) {
        return isBitOp(op) ? AsciiCodec::bitsSize(count) : AsciiCodec::wordsSize(count);
    }
    return isBitOp(op) ? BinaryCodec::bitsSize(count) : BinaryCodec::wordsSize(count);
}

size_t endCodeSize(DataCode code) {
    return code == DataCode::Ascii ? AsciiCodec::u16Size() : BinaryCodec::u16Size();
}

// Error codes of the captured PLCs (spec "Targets"), one named constant per code.
constexpr uint16_t kQCpuRefused = 0x4031; // 3E and 3C: out of range and type not supported
constexpr uint16_t kFx5OutOfRange3e = 0xC056;
constexpr uint16_t kFx5Type3e = 0xC05C;            // any type but DX and DY; DX and DY in Binary
constexpr uint16_t kFx5TypeDirectAscii3e = 0xC050; // DX and DY in ASCII
constexpr uint16_t kFx5OutOfRange3c = 0x7143;
constexpr uint16_t kFx5Type3c = 0x7E43;       // any type but DX and DY
constexpr uint16_t kFx5TypeDirect3c = 0x7F21; // DX and DY
constexpr uint16_t kWrongSum3c = 0x7F24;      // QCpu and Fx5
constexpr uint16_t kFx3OutOfRange1e = 0x58;   // no abnormal code follows
constexpr uint16_t kFx3Type1e = 0x56;
constexpr uint16_t kFx3WrongSum1c = 0x02;

// The error codes of one frame family (MockOptions, replaced by the codes of a MockTarget) and the
// most response data one response can carry.
struct Failures {
    uint16_t unsupported;
    uint16_t outOfRange;
    uint8_t outOfRangeAbnormal;
    bool truncateFaultCodeToByte; // 1E: the end code is a u8.
    size_t maxWireData;
    uint16_t unsupportedType;       // a device type switched off by setTypeSupported()
    uint16_t unsupportedTypeDirect; // the same for DX and DY
};

bool isDirectAccess(DeviceType t) { return t == DeviceType::DX || t == DeviceType::DY; }

// Checks, in order: executable, supported device type, bit op on a bit device, failRange faults,
// device limits, response size. A failed request changes no memory.
Outcome execute(const Access& req, DataCode code, MemoryImage& memory,
                const std::vector<Fault>& faults, const Failures& fail) {
    if (!req.executable) {
        return failure(fail.unsupported);
    }
    if (!memory.supported(req.head.type)) {
        return failure(isDirectAccess(req.head.type) ? fail.unsupportedTypeDirect
                                                     : fail.unsupportedType);
    }

    // A bit-unit command needs a bit device; word devices have no bit view (memory_image.h).
    const bool bitDevice = deviceInfo(req.head.type).kind == DeviceKind::Bit;
    if (isBitOp(req.op) && !bitDevice) {
        return failure(fail.unsupported);
    }

    // Points the request touches: one per bit, one per word, or 16 per word on a bit device.
    const uint64_t points = (bitDevice && !isBitOp(req.op)) ? uint64_t{req.count} * 16 : req.count;
    const uint64_t first = req.head.number;
    const uint64_t last = first + points - 1;

    for (const Fault& f : faults) {
        if (f.type == req.head.type && f.first <= last && f.last >= first) {
            return failure(fail.truncateFaultCodeToByte ? static_cast<uint16_t>(f.code & 0xFFu)
                                                        : f.code,
                           f.abnormal);
        }
    }
    if (memory.outOfRange(req.head.type, first, points)) {
        return failure(fail.outOfRange, fail.outOfRangeAbnormal);
    }
    if (!isWrite(req.op) && wireDataSize(code, req.op, req.count) > fail.maxWireData) {
        return failure(fail.unsupported); // the response length field cannot describe it
    }

    Outcome out;
    switch (req.op) {
    case Op::ReadWords:
        out.data.resize(size_t{req.count} * 2);
        for (size_t k = 0; k < req.count; ++k) {
            uint16_t w = memory.wordAt(req.head, k);
            out.data[2 * k] = static_cast<uint8_t>(w & 0xFFu);
            out.data[2 * k + 1] = static_cast<uint8_t>(w >> 8);
        }
        break;
    case Op::ReadBits:
        out.data.resize(req.count);
        for (size_t i = 0; i < req.count; ++i) {
            out.data[i] = memory.bitAt(req.head, i) ? 1 : 0;
        }
        break;
    case Op::WriteWords:
        for (size_t k = 0; k < req.count; ++k) {
            memory.setWordAt(req.head, k,
                             static_cast<uint16_t>(req.data[2 * k] | (req.data[2 * k + 1] << 8)));
        }
        break;
    case Op::WriteBits:
        for (size_t i = 0; i < req.count; ++i) {
            memory.setBitAt(req.head, i, req.data[i] != 0);
        }
        break;
    }
    return out;
}

} // namespace

Outcome executeQna(const QnaRequest& req, DataCode code, MemoryImage& memory,
                   const std::vector<Fault>& faults, const MockOptions& options) {
    // The 3E response length field is a u16 and counts the end code as well.
    Failures fail{options.unsupportedQna,
                  options.outOfRangeQna,
                  0,
                  false,
                  size_t{0xFFFFu} - endCodeSize(code),
                  options.unsupportedQna,
                  options.unsupportedQna};
    if (options.target == MockTarget::QCpu) {
        fail.outOfRange = fail.unsupportedType = fail.unsupportedTypeDirect = kQCpuRefused;
    } else if (options.target == MockTarget::Fx5) {
        fail.outOfRange = kFx5OutOfRange3e;
        fail.unsupportedType = kFx5Type3e;
        fail.unsupportedTypeDirect = code == DataCode::Ascii ? kFx5TypeDirectAscii3e : kFx5Type3e;
    }
    return execute(req, code, memory, faults, fail);
}

Outcome executeE1(const E1Request& req, MemoryImage& memory, const std::vector<Fault>& faults,
                  const MockOptions& options) {
    // A 1E response has no length field; the wire code is irrelevant to the size check.
    Failures fail{
        options.unsupported1e, options.outOfRange1e, options.outOfRange1eAbnormal, true, SIZE_MAX,
        options.unsupported1e, options.unsupported1e};
    if (options.target == MockTarget::Fx3) {
        fail.outOfRange = kFx3OutOfRange1e;
        fail.outOfRangeAbnormal = 0;
        fail.unsupportedType = fail.unsupportedTypeDirect = kFx3Type1e;
    }
    return execute(req, DataCode::Binary, memory, faults, fail);
}

Outcome executeSerial(const SerialRequest& req, FrameType frame, MemoryImage& memory,
                      const std::vector<Fault>& faults, const MockOptions& options) {
    // A serial response has no length field, so its size is not limited. The 1C error code is two
    // characters, a u8.
    const bool qna = frame == FrameType::F3C;
    const uint16_t unsupported = qna ? options.unsupportedQna : options.unsupported1c;
    const uint16_t outOfRange = qna ? options.outOfRangeQna : options.outOfRange1c;
    Failures fail{unsupported, outOfRange, 0, !qna, SIZE_MAX, unsupported, unsupported};
    if (qna && options.target == MockTarget::QCpu) {
        fail.outOfRange = fail.unsupportedType = fail.unsupportedTypeDirect = kQCpuRefused;
    } else if (qna && options.target == MockTarget::Fx5) {
        fail.outOfRange = kFx5OutOfRange3c;
        fail.unsupportedType = kFx5Type3c;
        fail.unsupportedTypeDirect = kFx5TypeDirect3c;
    }
    return execute(req, DataCode::Ascii, memory, faults, fail);
}

uint16_t wrongSumCode(FrameType frame, const MockOptions& options) {
    if (frame == FrameType::F3C) {
        const bool captured =
            options.target == MockTarget::QCpu || options.target == MockTarget::Fx5;
        return captured ? kWrongSum3c : options.sumErrorQna;
    }
    return options.target == MockTarget::Fx3 ? kFx3WrongSum1c : uint16_t{options.sumError1c};
}

} // namespace mc::detail::mock
