/**
 * @file hil_confirm_dialog.h
 * @brief `HilConfirmDialog`: the confirmation of a HIL run. One **Confirm** button; the operator
 * types nothing. Cancel or closing the dialog starts nothing.
 */
#pragma once

#include "mc_workbench/hil_types.h"

#include <QDialog>

class QPushButton;

namespace mc::workbench {

/**
 * @brief The dialog the "Run" button opens.
 *
 * It shows the profile id, the confirmation summary of `hil_capture` (PLC identity, transport
 * endpoint, scratch areas, read-only frames) and the dry run. The Confirm button is enabled
 * whenever the check passed; a refused or failed check has no Confirm (the dialog is built only for
 * a check that passed, and `confirmationAccepted()` refuses any other). Accepting the dialog is the
 * operator's confirmation; the runner repeats the gate on its own thread and also needs it.
 *
 * @note GUI thread only.
 */
class HilConfirmDialog : public QDialog {
    Q_OBJECT
public:
    /**
     * @brief Builds the dialog for a check.
     * @param[in] check The check the operator looked at.
     * @param[in] parent Parent widget.
     */
    explicit HilConfirmDialog(const HilCheckResult& check, QWidget* parent = nullptr);

    /// @brief Whether the Confirm button is enabled.
    /// @return true when the check passed.
    bool acceptEnabled() const;

    /// @brief The Confirm button.
    /// @return Never null.
    QPushButton* okButton() const { return m_ok; }

    /// @brief Accepts the dialog when the check passed; otherwise does nothing.
    void accept() override;

private:
    HilCheckResult m_check;
    QPushButton* m_ok;
};

} // namespace mc::workbench
