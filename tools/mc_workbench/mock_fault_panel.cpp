#include "mc_workbench/mock_fault_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mc::workbench {

namespace {

constexpr mc::Corruption kCorruptions[] = {
    mc::Corruption::WrongSubheader, mc::Corruption::WrongSumCheck, mc::Corruption::WrongRoute,
    mc::Corruption::WrongBlockNo,   mc::Corruption::Truncate,      mc::Corruption::JunkPrefix,
    mc::Corruption::ExtraByte};

QString symbolOf(mc::DeviceType type) {
    return QString::fromLatin1(mc::deviceInfo(type).symbol);
}

QComboBox* deviceTypeCombo(mc::DeviceType initial) {
    auto* combo = new QComboBox;
    const int count = static_cast<int>(mc::DeviceType::Count);
    for (int i = 0; i < count; ++i) {
        const auto type = static_cast<mc::DeviceType>(i);
        combo->addItem(symbolOf(type), i);
    }
    combo->setCurrentIndex(static_cast<int>(initial));
    return combo;
}

mc::DeviceType typeOf(const QComboBox* combo) {
    return static_cast<mc::DeviceType>(combo->currentData().toInt());
}

// A decimal or 0x-prefixed number in a line edit.
bool parseNumber(const QString& text, quint32 maximum, quint32& out) {
    bool ok = false;
    const QString t = text.trimmed();
    const qulonglong value = t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                                 ? t.mid(2).toULongLong(&ok, 16)
                                 : t.toULongLong(&ok);
    if (!ok || value > maximum) {
        return false;
    }
    out = static_cast<quint32>(value);
    return true;
}

} // namespace

QString MockFaultPanel::corruptionName(mc::Corruption mode) {
    switch (mode) {
    case mc::Corruption::WrongSubheader:
        return QStringLiteral("WrongSubheader");
    case mc::Corruption::WrongSumCheck:
        return QStringLiteral("WrongSumCheck");
    case mc::Corruption::WrongRoute:
        return QStringLiteral("WrongRoute");
    case mc::Corruption::WrongBlockNo:
        return QStringLiteral("WrongBlockNo");
    case mc::Corruption::Truncate:
        return QStringLiteral("Truncate");
    case mc::Corruption::JunkPrefix:
        return QStringLiteral("JunkPrefix");
    case mc::Corruption::ExtraByte:
        return QStringLiteral("ExtraByte");
    }
    return QStringLiteral("?");
}

MockFaultPanel::MockFaultPanel(QWidget* parent)
    : QWidget(parent), m_mute(new QCheckBox(QStringLiteral("Mute: swallow every request"))),
      m_muteCount(new QSpinBox), m_corruption(new QComboBox), m_corruptCount(new QSpinBox),
      m_faultType(deviceTypeCombo(mc::DeviceType::D)), m_faultFirst(new QLineEdit(QStringLiteral("0"))),
      m_faultLast(new QLineEdit(QStringLiteral("0"))), m_faultCode(new QLineEdit(QStringLiteral("0xC050"))),
      m_faultAbnormal(new QLineEdit(QStringLiteral("0"))),
      m_limitType(deviceTypeCombo(mc::DeviceType::D)), m_limit(new QSpinBox),
      m_faults(new QListWidget), m_hint(new QLineEdit) {
    m_hint->setReadOnly(true);
    m_hint->setFrame(false);
    m_hint->setStyleSheet(QStringLiteral("color: #b00020; background: transparent;"));

    m_muteCount->setRange(1, 1000000);
    m_muteCount->setValue(1);
    auto* muteNext = new QPushButton(QStringLiteral("Mute next"));
    muteNext->setToolTip(QStringLiteral("Swallow the next N requests without answering"));
    auto* muteRow = new QHBoxLayout;
    muteRow->addWidget(new QLabel(QStringLiteral("Requests")));
    muteRow->addWidget(m_muteCount);
    muteRow->addWidget(muteNext);
    muteRow->addStretch(1);
    auto* muteBox = new QGroupBox(QStringLiteral("No answer"));
    auto* muteLayout = new QVBoxLayout(muteBox);
    muteLayout->addWidget(m_mute);
    muteLayout->addLayout(muteRow);

    for (mc::Corruption mode : kCorruptions) {
        m_corruption->addItem(corruptionName(mode), static_cast<int>(mode));
    }
    m_corruptCount->setRange(1, 1000000);
    m_corruptCount->setValue(1);
    auto* corrupt = new QPushButton(QStringLiteral("Corrupt next"));
    corrupt->setToolTip(QStringLiteral("Damage the next N responses in the chosen way"));
    auto* corruptRow = new QHBoxLayout;
    corruptRow->addWidget(m_corruption);
    corruptRow->addWidget(new QLabel(QStringLiteral("Responses")));
    corruptRow->addWidget(m_corruptCount);
    corruptRow->addWidget(corrupt);
    corruptRow->addStretch(1);
    auto* corruptBox = new QGroupBox(QStringLiteral("Corruption"));
    corruptBox->setLayout(corruptRow);

    auto* addFault = new QPushButton(QStringLiteral("Add fault"));
    auto* clear = new QPushButton(QStringLiteral("Clear faults"));
    auto* faultRow = new QHBoxLayout;
    faultRow->addWidget(m_faultType);
    faultRow->addWidget(new QLabel(QStringLiteral("from")));
    faultRow->addWidget(m_faultFirst);
    faultRow->addWidget(new QLabel(QStringLiteral("to")));
    faultRow->addWidget(m_faultLast);
    faultRow->addWidget(new QLabel(QStringLiteral("end code")));
    faultRow->addWidget(m_faultCode);
    faultRow->addWidget(new QLabel(QStringLiteral("1E abnormal")));
    faultRow->addWidget(m_faultAbnormal);
    faultRow->addWidget(addFault);
    faultRow->addWidget(clear);
    auto* faultBox = new QGroupBox(QStringLiteral("PLC error on a device range"));
    auto* faultLayout = new QVBoxLayout(faultBox);
    faultLayout->addLayout(faultRow);
    faultLayout->addWidget(m_faults, 1);

    m_limit->setRange(0, 1000000);
    m_limit->setValue(1000);
    auto* setLimit = new QPushButton(QStringLiteral("Set limit"));
    setLimit->setToolTip(QStringLiteral("Points of this device type the mock accepts; beyond it the "
                                        "out-of-range code is answered"));
    auto* limitRow = new QHBoxLayout;
    limitRow->addWidget(m_limitType);
    limitRow->addWidget(new QLabel(QStringLiteral("points")));
    limitRow->addWidget(m_limit);
    limitRow->addWidget(setLimit);
    limitRow->addStretch(1);
    auto* limitBox = new QGroupBox(QStringLiteral("Device limit"));
    limitBox->setLayout(limitRow);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(muteBox);
    layout->addWidget(corruptBox);
    layout->addWidget(faultBox, 1);
    layout->addWidget(limitBox);
    layout->addWidget(m_hint);

    connect(m_mute, &QCheckBox::toggled, this, [this](bool on) { emit muteToggled(on); });
    connect(muteNext, &QPushButton::clicked, this,
            [this]() { emit muteNextRequested(static_cast<quint32>(m_muteCount->value())); });
    connect(corrupt, &QPushButton::clicked, this, [this]() {
        emit corruptRequested(static_cast<mc::Corruption>(m_corruption->currentData().toInt()),
                              static_cast<quint32>(m_corruptCount->value()));
    });
    connect(addFault, &QPushButton::clicked, this, [this]() { onAddFault(); });
    connect(clear, &QPushButton::clicked, this, [this]() {
        m_faults->clear();
        m_hint->clear();
        emit clearFaultsRequested();
    });
    connect(setLimit, &QPushButton::clicked, this, [this]() {
        emit deviceLimitRequested(typeOf(m_limitType), static_cast<quint32>(m_limit->value()));
    });
}

bool MockFaultPanel::isMuted() const {
    return m_mute->isChecked();
}

void MockFaultPanel::setMuted(bool on) {
    const QSignalBlocker blocker(m_mute);
    m_mute->setChecked(on);
}

QStringList MockFaultPanel::faultLines() const {
    QStringList lines;
    for (int i = 0; i < m_faults->count(); ++i) {
        lines.append(m_faults->item(i)->text());
    }
    return lines;
}

void MockFaultPanel::clearFaultLines() {
    m_faults->clear();
    m_hint->clear();
}

void MockFaultPanel::onAddFault() {
    quint32 first = 0;
    quint32 last = 0;
    quint32 code = 0;
    quint32 abnormal = 0;
    if (!parseNumber(m_faultFirst->text(), 0xFFFFFF, first) ||
        !parseNumber(m_faultLast->text(), 0xFFFFFF, last) ||
        !parseNumber(m_faultCode->text(), 0xFFFF, code) ||
        !parseNumber(m_faultAbnormal->text(), 0xFF, abnormal)) {
        m_hint->setText(QStringLiteral("numbers are decimal or 0x-hex; the end code fits 16 bits, the "
                                       "abnormal code 8 bits"));
        return;
    }
    if (first > last) {
        m_hint->setText(QStringLiteral("the first device number is above the last"));
        return;
    }
    m_hint->clear();
    requestFailRange(typeOf(m_faultType), first, last, static_cast<quint16>(code),
                     static_cast<quint8>(abnormal));
}

void MockFaultPanel::requestFailRange(mc::DeviceType type, quint32 first, quint32 last,
                                      quint16 code, quint8 abnormal) {
    m_faults->addItem(QStringLiteral("%1%2..%3 -> end code 0x%4%5")
                          .arg(symbolOf(type))
                          .arg(first)
                          .arg(last)
                          .arg(QString::number(code, 16).toUpper().rightJustified(4, QLatin1Char('0')))
                          .arg(abnormal != 0 ? QStringLiteral(" (abnormal 0x%1)")
                                                   .arg(QString::number(abnormal, 16).toUpper().rightJustified(2, QLatin1Char('0')))
                                             : QString()));
    emit failRangeRequested(type, first, last, code, abnormal);
}

} // namespace mc::workbench
