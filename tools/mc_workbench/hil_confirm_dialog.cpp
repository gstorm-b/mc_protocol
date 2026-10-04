#include "mc_workbench/hil_confirm_dialog.h"

#include "mc_workbench/hil_prepare.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace mc::workbench {

HilConfirmDialog::HilConfirmDialog(const HilCheckResult& check, QWidget* parent)
    : QDialog(parent), m_check(check) {
    setWindowTitle(QStringLiteral("Confirm HIL run"));
    resize(720, 480);
    auto* layout = new QVBoxLayout(this);

    auto* summary = new QPlainTextEdit(check.confirmationText);
    summary->setReadOnly(true);
    summary->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(summary, 1);

    if (!check.readOnlyFrames.isEmpty()) {
        auto* warn = new QLabel(
            QStringLiteral("This run holds %1 read-only frame(s) the gate could not decode and "
                           "takes the plan's word for. The profile id must be typed.")
                .arg(check.readOnlyFrames.size()));
        warn->setWordWrap(true);
        layout->addWidget(warn);
    }

    auto* prompt = new QLabel(QStringLiteral("Type the profile id (<b>%1</b>) to confirm:")
                                  .arg(check.profileId.toHtmlEscaped()));
    prompt->setTextFormat(Qt::RichText);
    layout->addWidget(prompt);
    m_typed = new QLineEdit;
    m_typed->setPlaceholderText(check.profileId);
    layout->addWidget(m_typed);

    if (skipTypingAllowed(check)) {
        m_skip = new QCheckBox(QStringLiteral(
            "Repeat run of an already-checked profile: confirm without typing (like --yes)"));
        layout->addWidget(m_skip);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_ok = buttons->addButton(QStringLiteral("Run"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &HilConfirmDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_typed, &QLineEdit::textChanged, this, [this]() { update(); });
    if (m_skip != nullptr) {
        connect(m_skip, &QCheckBox::toggled, this, [this]() { update(); });
    }
    update();
}

QString HilConfirmDialog::typedId() const {
    return m_typed->text();
}

bool HilConfirmDialog::skipTypingAvailable() const {
    return m_skip != nullptr;
}

bool HilConfirmDialog::skipTyping() const {
    return m_skip != nullptr && m_skip->isChecked();
}

bool HilConfirmDialog::acceptEnabled() const {
    return confirmationAccepted(m_check, typedId(), skipTyping());
}

void HilConfirmDialog::typeId(const QString& text) {
    m_typed->setText(text);
}

void HilConfirmDialog::setSkipTyping(bool on) {
    if (m_skip != nullptr) {
        m_skip->setChecked(on);
    }
}

void HilConfirmDialog::accept() {
    if (acceptEnabled()) {
        QDialog::accept();
    }
}

void HilConfirmDialog::update() {
    m_ok->setEnabled(acceptEnabled());
}

} // namespace mc::workbench
