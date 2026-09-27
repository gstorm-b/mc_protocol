// src/core/session/output_ring.h — fixed-capacity ring of Output records (spec
// SPEC-core-session.md, Project Structure). Session's own private implementation detail:
// include/mc/core/session.h only forward-declares mc::detail::OutputRing and holds it behind a
// std::unique_ptr, since a public header must never include a src/-only path (BLD-05).
//
// "+ change arena" (the Project Structure's own description of this file) is not built yet: it
// is the scratch storage a produced ValuesChanged/Snapshot Output's raw pointers (Output::changes
// / Output::chunks) would need to stay valid until the next input call. Nothing in this task
// (T-022) ever produces those two OutputKinds (that is T-023's "Publishing values" job), so
// nothing here needs it yet; T-023 is expected to extend this file when it starts populating
// them.
#pragma once

#include "mc/core/session.h"

#include <cstddef>
#include <vector>

namespace mc::detail {

/// A circular buffer of `Output` records, sized once at construction and never grown; `push()`
/// overwrites nothing as long as the caller keeps within `capacity()` (Session sizes it for the
/// worst single input call, per the module spec's own "Drain contract").
class OutputRing {
public:
    explicit OutputRing(size_t capacity) : m_items(capacity) {}

    /// @pre `size() < capacity()`.
    void push(const Output& o) noexcept {
        size_t idx = (m_head + m_count) % m_items.size();
        m_items[idx] = o;
        ++m_count;
    }

    /// @return `false` when empty (`out` left unchanged).
    bool pop(Output& out) noexcept {
        if (m_count == 0) {
            return false;
        }
        out = m_items[m_head];
        m_head = (m_head + 1) % m_items.size();
        --m_count;
        return true;
    }

    bool empty() const noexcept { return m_count == 0; }
    size_t size() const noexcept { return m_count; }
    size_t capacity() const noexcept { return m_items.size(); }

private:
    std::vector<Output> m_items;
    size_t m_head{0};
    size_t m_count{0};
};

} // namespace mc::detail
