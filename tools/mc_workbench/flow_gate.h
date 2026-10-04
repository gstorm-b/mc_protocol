/**
 * @file flow_gate.h
 * @brief `FlowGate`: the credit counter of the back-pressure between a runner and the GUI thread.
 */
#pragma once

namespace mc::workbench {

/**
 * @brief Lets a runner emit a batch only while the GUI thread has caught up with the last one.
 *
 * A runner calls sent() when it emits a batch and canEmit() before the next one; the GUI side
 * answers every batch with an acknowledgement that runs acked() on the runner thread. While one
 * batch is unacknowledged the runner keeps collecting (in bounded queues) instead of queueing more
 * events on the GUI thread, so a slow GUI cannot make the event queue grow without limit.
 *
 * A disabled gate (the default) never blocks: runners used without a host behave as before.
 *
 * @note Runner thread only; no locking.
 */
class FlowGate {
public:
    /// @brief The batches that may be unacknowledged at one time.
    static constexpr int kMaxInFlight = 1;

    /// @brief Turns the gate on or off; turning it off forgets what is in flight.
    /// @param[in] on true to enforce credits.
    void setEnabled(bool on) noexcept {
        m_enabled = on;
        m_inFlight = 0;
    }

    /// @brief Whether the gate enforces credits.
    /// @return true when enabled.
    bool enabled() const noexcept { return m_enabled; }

    /// @brief Whether a batch may be emitted now.
    /// @return true when the gate is off or the GUI has acknowledged the last batch.
    bool canEmit() const noexcept { return !m_enabled || m_inFlight < kMaxInFlight; }

    /// @brief Records that a batch was emitted.
    void sent() noexcept {
        if (m_enabled) {
            ++m_inFlight;
        }
    }

    /// @brief Records the GUI's acknowledgement of a batch.
    void acked() noexcept {
        if (m_inFlight > 0) {
            --m_inFlight;
        }
    }

    /// @brief Batches emitted and not yet acknowledged.
    /// @return The count.
    int inFlight() const noexcept { return m_inFlight; }

private:
    bool m_enabled{false};
    int m_inFlight{0};
};

} // namespace mc::workbench
