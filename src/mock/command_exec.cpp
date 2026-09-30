// Batch read and write of a decoded QnA request on the memory image, with the fault and limit
// lookups that turn a request into a PLC error.
#include "mock/mock_internal.h"

#include "core/protocol/field_codec.h"

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

} // namespace

Outcome executeQna(const QnaRequest& req, DataCode code, MemoryImage& memory,
                   const std::vector<Fault>& faults, const MockOptions& options) {
    if (!req.executable) {
        return failure(options.unsupportedQna);
    }

    // A bit-unit command needs a bit device; word devices have no bit view (memory_image.h).
    const bool bitDevice = deviceInfo(req.head.type).kind == DeviceKind::Bit;
    if (isBitOp(req.op) && !bitDevice) {
        return failure(options.unsupportedQna);
    }

    // Points the request touches: one per bit, one per word, or 16 per word on a bit device.
    const uint64_t points = (bitDevice && !isBitOp(req.op)) ? uint64_t{req.count} * 16 : req.count;
    const uint64_t first = req.head.number;
    const uint64_t last = first + points - 1;

    for (const Fault& f : faults) {
        if (f.type == req.head.type && f.first <= last && f.last >= first) {
            return failure(f.code, f.abnormal);
        }
    }
    if (memory.outOfRange(req.head.type, first, points)) {
        return failure(options.outOfRangeQna);
    }
    if (!isWrite(req.op) &&
        endCodeSize(code) + wireDataSize(code, req.op, req.count) > 0xFFFFu) {
        return failure(options.unsupportedQna); // the response length field is a u16
    }

    Outcome out;
    switch (req.op) {
    case Op::ReadWords:
        out.data.resize(size_t{req.count} * 2);
        for (uint64_t k = 0; k < req.count; ++k) {
            uint16_t w = memory.wordAt(req.head, k);
            out.data[2 * k] = static_cast<uint8_t>(w & 0xFFu);
            out.data[2 * k + 1] = static_cast<uint8_t>(w >> 8);
        }
        break;
    case Op::ReadBits:
        out.data.resize(req.count);
        for (uint64_t i = 0; i < req.count; ++i) {
            out.data[i] = memory.bitAt(req.head, i) ? 1 : 0;
        }
        break;
    case Op::WriteWords:
        for (uint64_t k = 0; k < req.count; ++k) {
            memory.setWordAt(req.head, k,
                             static_cast<uint16_t>(req.data[2 * k] | (req.data[2 * k + 1] << 8)));
        }
        break;
    case Op::WriteBits:
        for (uint64_t i = 0; i < req.count; ++i) {
            memory.setBitAt(req.head, i, req.data[i] != 0);
        }
        break;
    }
    return out;
}

} // namespace mc::detail::mock
