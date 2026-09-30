// tests/mock/integration/pipe.h (SPEC-mock-plc.md "Integration"): an in-memory byte pipe that
// splits every transfer into random fragments from a fixed seed, so a Session <-> MockPlc run is
// exactly reproducible and still exercises arbitrary fragmentation of every frame.
#pragma once

#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <random>
#include <utility>

namespace mc::test {

class Pipe {
public:
    /// `seed` fixes the fragmentation; `maxFragment` bounds one fragment's size in bytes.
    explicit Pipe(uint32_t seed, size_t maxFragment = 16)
        : m_rng(seed), m_maxFragment(maxFragment) {}

    /// Appends `bytes`, cut into fragments of 1..maxFragment bytes.
    void write(ByteView bytes) {
        size_t pos = 0;
        while (pos < bytes.size) {
            // Raw mt19937 output, not a std distribution: distributions are implementation-defined
            // and would fragment differently on MSVC and GCC.
            size_t n = 1 + static_cast<size_t>(m_rng()) % m_maxFragment;
            if (n > bytes.size - pos) {
                n = bytes.size - pos;
            }
            m_fragments.emplace_back(bytes.data + pos, bytes.data + pos + n);
            ++m_fragmentsWritten;
            pos += n;
        }
    }

    bool empty() const noexcept { return m_fragments.empty(); }

    /// Pops the oldest fragment into `out`; false when the pipe is empty.
    bool read(ByteBuf& out) {
        if (m_fragments.empty()) {
            return false;
        }
        out = std::move(m_fragments.front());
        m_fragments.pop_front();
        return true;
    }

    /// Number of fragments written so far.
    size_t fragmentsWritten() const noexcept { return m_fragmentsWritten; }

private:
    std::mt19937 m_rng;
    size_t m_maxFragment;
    std::deque<ByteBuf> m_fragments;
    size_t m_fragmentsWritten{0};
};

} // namespace mc::test
