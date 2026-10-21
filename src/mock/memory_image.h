// Private to mc_mock (mc::detail::mock): the device memory image behind MockPlc.
#pragma once

#include "mc/core/device.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mc::detail::mock {

/// Sparse device memory. Every DeviceType has its own address space of 2^32 points; pages are
/// allocated on the first write and unwritten memory reads as 0. A bit device stores one point
/// per entry; word access to it sees bit i of word k at head + 16k + i (spec §2.4). A word
/// device has no bit view: bit reads give false and bit writes are ignored. Numbers are never
/// aliased between device types.
class MemoryImage {
public:
    /// Exclusive end of every device's address space, and the default limit.
    static constexpr uint64_t kAddressSpace = uint64_t{1} << 32;

    /// Word k counted from `head`: the word itself on a word device, the 16 points
    /// head+16k .. head+16k+15 (bit 0 = head+16k) on a bit device.
    uint16_t wordAt(const Device& head, uint64_t k) const;
    void setWordAt(const Device& head, uint64_t k, uint16_t v);

    /// Point head+i of a bit device; always false / ignored on a word device.
    bool bitAt(const Device& head, uint64_t i) const;
    void setBitAt(const Device& head, uint64_t i, bool v);

    /// Points of type `t` with number >= `limit` do not exist.
    void setLimit(DeviceType t, uint32_t limit);

    /// Points [from, limit) of type `t` exist in addition to those below the limit set by
    /// setLimit(); `from >= limit` removes the range.
    void setSpecialRange(DeviceType t, uint32_t from, uint32_t limit) noexcept;

    /// Whether any point of [first, first + pointCount) does not exist: the range is not wholly
    /// below the limit of `t` nor wholly inside its special range. An empty range is never out of
    /// range.
    bool outOfRange(DeviceType t, uint64_t first, uint64_t pointCount) const;

    /// Switches type `t` on or off (all are supported until switched off).
    void setSupported(DeviceType t, bool supported) noexcept;

    /// Whether type `t` is supported.
    bool supported(DeviceType t) const noexcept;

private:
    using Pages = std::unordered_map<uint32_t, std::vector<uint16_t>>;

    uint16_t raw(DeviceType t, uint64_t n) const;
    void setRaw(DeviceType t, uint64_t n, uint16_t v);

    std::array<Pages, static_cast<size_t>(DeviceType::Count)> m_pages;
    std::array<uint64_t, static_cast<size_t>(DeviceType::Count)> m_limit = makeLimits();
    // The special range [from, limit) of each type; empty (0, 0) when none.
    std::array<uint64_t, static_cast<size_t>(DeviceType::Count)> m_specialFrom{};
    std::array<uint64_t, static_cast<size_t>(DeviceType::Count)> m_specialLimit{};
    std::array<bool, static_cast<size_t>(DeviceType::Count)> m_unsupported{}; // true = refused

    static std::array<uint64_t, static_cast<size_t>(DeviceType::Count)> makeLimits() {
        std::array<uint64_t, static_cast<size_t>(DeviceType::Count)> a{};
        a.fill(kAddressSpace);
        return a;
    }
};

} // namespace mc::detail::mock
