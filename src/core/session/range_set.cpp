// RangeSet::add()/remove()/size()/empty() (spec `SPEC-core-session.md`, poll_plan.h): plain
// subscription bookkeeping, no FrameConfig involved (that is ReadPlan::build()'s job,
// read_plan.cpp). Entries are kept sorted by (head.type, head.number) so ReadPlan::build() can
// walk one device type's subscriptions as a contiguous run, in DeviceType declaration order,
// without a separate grouping pass.
#include "mc/core/poll_plan.h"

#include <algorithm>
#include <cstdint>

namespace mc {
namespace {

Error pointCountError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::PointCount;
    e.message = message;
    return e;
}

Error invalidDeviceError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::InvalidDevice;
    e.message = message;
    return e;
}

Error notSubscribedError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Config;
    e.code = ErrorCode::NotSubscribed;
    e.message = message;
    return e;
}

} // namespace

Expected<SubscriptionId> RangeSet::add(Device head, uint32_t count) {
    if (count == 0) {
        return Expected<SubscriptionId>(pointCountError("count must be at least 1"));
    }

    // uint64_t so head.number == 0, count == UINT32_MAX (the widest legal request) does not
    // itself overflow before the range check below runs.
    uint64_t last = static_cast<uint64_t>(head.number) + static_cast<uint64_t>(count) - 1;
    if (last > 0xFFFFFFFFULL) {
        return Expected<SubscriptionId>(
            invalidDeviceError("head.number + count - 1 does not fit a 32-bit device number"));
    }

    SubscriptionId id = m_nextId++;
    Entry e{id, head, count};
    // A local lambda (rather than a free function in the anonymous namespace above) so it can
    // access Entry, a private nested type -- a lambda written inside a member function has the
    // same access as the function itself.
    auto entryLess = [](const Entry& a, const Entry& b) noexcept {
        if (a.head.type != b.head.type) {
            return static_cast<uint8_t>(a.head.type) < static_cast<uint8_t>(b.head.type);
        }
        return a.head.number < b.head.number;
    };
    auto it = std::lower_bound(m_entries.begin(), m_entries.end(), e, entryLess);
    m_entries.insert(it, e);
    return Expected<SubscriptionId>(id);
}

Expected<void> RangeSet::remove(SubscriptionId id) {
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                            [id](const Entry& e) { return e.id == id; });
    if (it == m_entries.end()) {
        return Expected<void>(notSubscribedError("no subscription with this id"));
    }
    m_entries.erase(it);
    return Expected<void>();
}

size_t RangeSet::size() const noexcept { return m_entries.size(); }

bool RangeSet::empty() const noexcept { return m_entries.empty(); }

} // namespace mc
