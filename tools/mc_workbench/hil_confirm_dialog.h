/**
 * @file hil_confirm_dialog.h
 * @brief `HilConfirmDialog`: the confirmation of a HIL run. The profile id has to be typed; only
 * a repeated run without read-only frames may be confirmed without typing, exactly as
 * `hil_capture --yes` allows.
 */
#pragma once

#include "mc_workbench/hil_types.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QLineEdit;
class QPushButton;

namespace mc::workbench {

/**
 * @brief The dialog the "Run" button opens.
 *
 * It shows the confirmation summary of `hil_capture` (PLC identity, transport, scratch area,
 * read-only frames). Whether the OK button works is decided by `confirmationAccepted()`, the rule
 * `runTool()` applies; the "confirm without typing" box exists only for a loopback TCP profile
 * whose run holds no read-only frame (`skipTypingAllowed()`). For any other profile (another host,
 * a COM port) or a run with read-only frames nothing but the exact profile id enables the button.
 * The runner checks the same rule again before it starts.
 *
 * @note GUI thread only.
 */
class HilConfirmDialog : public QDialog {
    Q_OBJECT
public:
    /**
     * @brief Builds the dialog for a check that passed the gate.
     * @param[in] check The check the operator looked at.
     * @param[in] parent Parent widget.
     */
    explicit HilConfirmDialog(const HilCheckResult& check, QWidget* parent = nullptr);

    /// @brief What is typed in the line edit.
    /// @return The text.
    QString typedId() const;

    /// @brief Whether the operator asked to confirm without typing and may.
    /// @return false when the box does not exist (read-only frames, or not a loopback profile),
    /// whatever was asked.
    bool skipTyping() const;

    /// @brief Whether the "confirm without typing" box exists (loopback TCP profile, no read-only
    /// frame).
    /// @return true when it does.
    bool skipTypingAvailable() const;

    /// @brief Whether the OK button is enabled.
    /// @return true when the confirmation rule holds right now.
    bool acceptEnabled() const;

    /// @brief Types into the line edit, as the operator would.
    /// @param[in] text The text.
    void typeId(const QString& text);

    /// @brief Ticks or clears the "confirm without typing" box; ignored when it does not exist.
    /// @param[in] on The new state.
    void setSkipTyping(bool on);

    /// @brief The OK button (labelled "Run").
    /// @return Never null.
    QPushButton* okButton() const { return m_ok; }

    /// @brief Accepts the dialog when the confirmation rule holds; otherwise does nothing.
    void accept() override;

private:
    void update();

    HilCheckResult m_check;
    QLineEdit* m_typed;
    QCheckBox* m_skip{nullptr};
    QPushButton* m_ok;
};

} // namespace mc::workbench
