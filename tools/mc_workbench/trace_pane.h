/**
 * @file trace_pane.h
 * @brief `TracePane`: the "Frame trace" dock: pick a tab, see its wire chunks, capture and export.
 */
#pragma once

#include <QWidget>

class QComboBox;

namespace mc::workbench {

class CapturePanel;
class TabTelemetry;
class TelemetryRegistry;
class TraceView;

/**
 * @brief The Frame trace dock content: a tab selector, the `TraceView` of the selected tab and its
 * `CapturePanel`.
 *
 * Follows the registry: a new tab appears in the selector (and becomes the selection when none is
 * selected); a tab that is closed leaves it.
 *
 * @note GUI thread only.
 */
class TracePane : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Builds the pane over @p registry.
     * @param[in] registry The window's registry; not owned, must outlive the pane.
     * @param[in] parent Parent widget.
     */
    explicit TracePane(TelemetryRegistry* registry, QWidget* parent = nullptr);

    /// @brief The trace table and toolbar.
    /// @return Never null.
    TraceView* traceView() const noexcept { return m_trace; }

    /// @brief The capture controls.
    /// @return Never null.
    CapturePanel* capturePanel() const noexcept { return m_capture; }

    /// @brief The tab shown.
    /// @return Null when none.
    TabTelemetry* current() const;

    /// @brief Shows a tab, as the selector would.
    /// @param[in] telemetry A registered tab; ignored otherwise.
    void select(TabTelemetry* telemetry);

private:
    void refill();
    void onSelected(int index);

    TelemetryRegistry* m_registry;
    QComboBox* m_tabs;
    TraceView* m_trace;
    CapturePanel* m_capture;
};

} // namespace mc::workbench
