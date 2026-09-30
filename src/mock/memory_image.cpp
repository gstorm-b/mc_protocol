#include "mock/memory_image.h"

namespace mc::detail::mock {

namespace {

constexpr uint32_t kPageShift = 12;
constexpr uint32_t kPageSize = uint32_t{1} << kPageShift;
constexpr uint64_t kPageMask = kPageSize - 1;

size_t indexOf(DeviceType t) { return static_cast<size_t>(t); }

bool isBitDevice(DeviceType t) { return deviceInfo(t).kind == DeviceKind::Bit; }

} // namespace

uint16_t MemoryImage::raw(DeviceType t, uint64_t n) const {
    if (indexOf(t) >= m_pages.size() || n >= kAddressSpace) {
        return 0;
    }
    const Pages& pages = m_pages[indexOf(t)];
    auto it = pages.find(static_cast<uint32_t>(n >> kPageShift));
    return it == pages.end() ? uint16_t{0} : it->second[n & kPageMask];
}

void MemoryImage::setRaw(DeviceType t, uint64_t n, uint16_t v) {
    if (indexOf(t) >= m_pages.size() || n >= kAddressSpace) {
        return;
    }
    std::vector<uint16_t>& page = m_pages[indexOf(t)][static_cast<uint32_t>(n >> kPageShift)];
    if (page.empty()) {
        page.assign(kPageSize, 0);
    }
    page[n & kPageMask] = v;
}

uint16_t MemoryImage::wordAt(const Device& head, uint64_t k) const {
    if (!isBitDevice(head.type)) {
        return raw(head.type, head.number + k);
    }
    uint16_t word = 0;
    for (uint64_t i = 0; i < 16; ++i) {
        if (bitAt(head, 16 * k + i)) {
            word = static_cast<uint16_t>(word | (uint16_t{1} << i));
        }
    }
    return word;
}

void MemoryImage::setWordAt(const Device& head, uint64_t k, uint16_t v) {
    if (!isBitDevice(head.type)) {
        setRaw(head.type, head.number + k, v);
        return;
    }
    for (uint64_t i = 0; i < 16; ++i) {
        setBitAt(head, 16 * k + i, ((v >> i) & 1u) != 0);
    }
}

bool MemoryImage::bitAt(const Device& head, uint64_t i) const {
    return isBitDevice(head.type) && raw(head.type, head.number + i) != 0;
}

void MemoryImage::setBitAt(const Device& head, uint64_t i, bool v) {
    if (isBitDevice(head.type)) {
        setRaw(head.type, head.number + i, v ? uint16_t{1} : uint16_t{0});
    }
}

void MemoryImage::setLimit(DeviceType t, uint32_t limit) {
    if (indexOf(t) < m_limit.size()) {
        m_limit[indexOf(t)] = limit;
    }
}

bool MemoryImage::outOfRange(DeviceType t, uint64_t first, uint64_t pointCount) const {
    if (pointCount == 0 || indexOf(t) >= m_limit.size()) {
        return false;
    }
    return first + pointCount > m_limit[indexOf(t)];
}

} // namespace mc::detail::mock
