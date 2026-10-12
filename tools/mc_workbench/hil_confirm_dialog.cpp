#include "mc_workbench/hil_confirm_dialog.h"

#include "mc_workbench/hil_prepare.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace mc::workbench {

HilConfirmDialog::HilConfirmDialog(const HilCheckResult& check, QWidget* parent)
    : QDialog(parent), m_check(check) {
    setWindowTitle(QStringLiteral("Confirm HIL run"));
    resize(720, 560);
    auto* layout = new QVBoxLayout(this);

    auto* head = new QLabel(QStringLiteral("Profile <b>%1</b>, plan <b>%2</b>: %3 step(s). Press "
                                           "Confirm to send the frames of the dry run.")
                                .arg(check.profileId.toHtmlEscaped(), check.planId.toHtmlEscaped())
                                .arg(check.stepCount));
    head->setTextFormat(Qt::RichText);
    head->setWordWrap(true);
    layout->addWidget(head);

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    // Transport (host:port or COM port and line settings) and scratch areas are in this text.
    auto* summary = new QPlainTextEdit(check.confirmationText);
    summary->setReadOnly(true);
    summary->setFont(fixed);
    layout->addWidget(summary, 1);

    layout->addWidget(new QLabel(QStringLiteral("Dry run:")));
    auto* dry = new QPlainTextEdit(check.dryRunText);
    dry->setReadOnly(true);
    dry->setFont(fixed);
    layout->addWidget(dry, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_ok = buttons->addButton(QStringLiteral("Confirm"), QDialogButtonBox::AcceptRole);
    // Enter must not start a run: Cancel is the default button, Confirm needs a click.
    m_ok->setAutoDefault(false);
    m_ok->setDefault(false);
    if (QPushButton* cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setAutoDefault(true);
        cancel->setDefault(true);
    }
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &HilConfirmDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_ok->setEnabled(acceptEnabled());
}

bool HilConfirmDialog::acceptEnabled() const {
    return confirmationAccepted(m_check, true);
}

void HilConfirmDialog::accept() {
    if (acceptEnabled()) {
        QDialog::accept();
    }
}

} // namespace mc::workbench
