#include "sumcheck.h"

#include "hexascii.h"

namespace mc::detail {

uint8_t sumcheck(ByteView data) noexcept {
    // Accumulates wider than 8 bits on purpose (PRIM-09: 300 bytes of FF wraps past 0xFF several
    // times); only the final `& 0xFF` (the cast below) keeps the low 8 bits, per spec §2.5.
    uint32_t sum = 0;
    for (size_t i = 0; i < data.size; ++i) {
        sum += data.data[i];
    }
    return static_cast<uint8_t>(sum & 0xFFu);
}

Expected<size_t> sumcheckEncode(ByteView data, MutableByteView out) noexcept {
    uint8_t sum = sumcheck(data);
    return hexEncode(ByteView{&sum, 1}, out);
}

} // namespace mc::detail
