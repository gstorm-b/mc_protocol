#include "mc_workbench/mock_memory_editor.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <string_view>

namespace mc::workbench {

namespace {

enum Column { ColDevice = 0, ColValue = 1 };

bool parseHead(const QString& text, mc::XyNumbering xy, mc::Device& out) {
    const QByteArray latin = text.trimmed().toLatin1();
    const auto parsed = mc::parseDevice(
        std::string_view(latin.constData(), static_cast<size_t>(latin.size())), xy);
    if (!parsed) {
        return false;
    }
    out = parsed.value();
    return true;
}

} // namespace

MockMemoryEditor::MockMemoryEditor(QWidget* parent)
    : QWidget(parent), m_headEdit(new QLineEdit(QStringLiteral("D0"))), m_countSpin(new QSpinBox),
      m_hex(new QCheckBox(QStringLiteral("Hex"))), m_hint(new QLabel), m_table(new QTableWidget),
      m_timer(new QTimer(this)) {
    m_countSpin->setRange(1, kMaxPoints);
    m_countSpin->setValue(m_count);
    m_headEdit->setMaximumWidth(120);
    m_headEdit->setToolTip(QStringLiteral("Head device, e.g. D100, M0, X10 (octal on FX CPUs)"));
    m_hex->setToolTip(QStringLiteral("Show words in hexadecimal"));

    auto* refresh = new QPushButton(QStringLiteral("Refresh"));
    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(QStringLiteral("Device")));
    bar->addWidget(m_headEdit);
    bar->addWidget(new QLabel(QStringLiteral("Points")));
    bar->addWidget(m_countSpin);
    bar->addWidget(m_hex);
    bar->addWidget(refresh);
    bar->addWidget(m_hint, 1);

    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Device"), QStringLiteral("Value")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(m_table, 1);

    m_timer->setInterval(kRefreshMs);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            refreshNow();
        }
    });
    connect(m_headEdit, &QLineEdit::editingFinished, this, [this]() { onRangeEntered(); });
    connect(m_countSpin, &QSpinBox::editingFinished, this, [this]() { onRangeEntered(); });
    connect(m_hex, &QCheckBox::toggled, this, [this]() { refreshNow(); });
    connect(refresh, &QPushButton::clicked, this, [this]() { refreshNow(); });
    connect(m_table, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem* item) { onItemChanged(item); });
    (void)parseHead(m_head, m_xy, m_device);
    rebuild();
}

void MockMemoryEditor::setXyNotation(mc::XyNumbering notation) {
    m_xy = notation;
    // The same text may mean another point now: parse it again and rebuild the rows.
    if (!setRange(m_head, m_count)) {
        m_hint->setText(QStringLiteral("the device text is not valid in this notation"));
    }
}

bool MockMemoryEditor::setRange(const QString& head, int count) {
    mc::Device device;
    if (count < 1 || count > kMaxPoints || !parseHead(head, m_xy, device)) {
        m_hint->setText(QStringLiteral("not a device, or a count out of range"));
        m_headEdit->setText(m_head);
        m_countSpin->setValue(m_count);
        return false;
    }
    m_hint->clear();
    m_device = device;
    m_head = head.trimmed();
    m_count = count;
    m_bits = mc::deviceInfo(device.type).kind == mc::DeviceKind::Bit;
    m_headEdit->setText(m_head);
    m_countSpin->setValue(m_count);
    rebuild();
    return true;
}

QString MockMemoryEditor::head() const {
    return m_head;
}

int MockMemoryEditor::count() const {
    return m_count;
}

void MockMemoryEditor::setAutoRefresh(bool on) {
    if (on) {
        m_timer->start();
        refreshNow();
    } else {
        m_timer->stop();
    }
}

void MockMemoryEditor::onRangeEntered() {
    if (m_headEdit->text().trimmed() == m_head && m_countSpin->value() == m_count) {
        return;
    }
    if (setRange(m_headEdit->text(), m_countSpin->value())) {
        refreshNow();
    }
}

QString MockMemoryEditor::deviceName(int row) const {
    mc::Device device = m_device;
    device.number += static_cast<uint32_t>(row);
    char buffer[16];
    const size_t size = mc::formatDevice(device, buffer, sizeof(buffer), m_xy);
    return QString::fromLatin1(buffer, static_cast<QString::size_type>(size));
}

QString MockMemoryEditor::valueText(quint16 value) const {
    if (m_bits) {
        return QString::number(value != 0 ? 1 : 0);
    }
    if (m_hex->isChecked()) {
        return QStringLiteral("0x") + QStringLiteral("%1").arg(value, 4, 16, QLatin1Char('0')).toUpper();
    }
    return QString::number(value);
}

bool MockMemoryEditor::parseValue(const QString& text, quint16& value) const {
    bool ok = false;
    const QString t = text.trimmed();
    uint parsed = 0;
    if (m_bits) {
        parsed = t.toUInt(&ok);
        ok = ok && parsed <= 1;
    } else if (t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        parsed = t.mid(2).toUInt(&ok, 16);
        ok = ok && parsed <= 0xFFFF;
    } else {
        parsed = t.toUInt(&ok);
        ok = ok && parsed <= 0xFFFF;
    }
    if (ok) {
        value = static_cast<quint16>(parsed);
    }
    return ok;
}

void MockMemoryEditor::rebuild() {
    m_updating = true;
    m_table->setRowCount(m_count);
    for (int row = 0; row < m_count; ++row) {
        auto* name = new QTableWidgetItem(deviceName(row));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, ColDevice, name);
        m_table->setItem(row, ColValue, new QTableWidgetItem(valueText(0)));
    }
    m_updating = false;
}

void MockMemoryEditor::refreshNow() {
    emit readRequested(m_head, static_cast<quint16>(m_count), m_bits);
}

void MockMemoryEditor::showBlock(const MemoryBlock& block) {
    if (block.head != m_head || block.bits != m_bits || block.values.size() != m_count) {
        return; // an answer for a range that is not shown any more
    }
    m_updating = true;
    // An open cell editor is a QLineEdit child of the viewport; its cell is left alone.
    const QTableWidgetItem* editing =
        m_table->viewport()->findChild<QLineEdit*>() != nullptr ? m_table->currentItem() : nullptr;
    for (int row = 0; row < m_count; ++row) {
        QTableWidgetItem* item = m_table->item(row, ColValue);
        if (item != nullptr && item != editing) {
            item->setText(valueText(block.values.at(row)));
        }
    }
    m_updating = false;
}

quint16 MockMemoryEditor::valueAt(int row) const {
    if (row < 0 || row >= m_count) {
        return 0;
    }
    quint16 value = 0;
    (void)parseValue(m_table->item(row, ColValue)->text(), value);
    return value;
}

bool MockMemoryEditor::editRow(int row, quint16 value) {
    if (row < 0 || row >= m_count) {
        return false;
    }
    m_table->item(row, ColValue)->setText(valueText(value)); // fires itemChanged
    return true;
}

void MockMemoryEditor::onItemChanged(QTableWidgetItem* item) {
    if (m_updating || item == nullptr || item->column() != ColValue) {
        return;
    }
    quint16 value = 0;
    if (!parseValue(item->text(), value)) {
        m_hint->setText(m_bits ? QStringLiteral("a bit is 0 or 1")
                               : QStringLiteral("a word is 0 to 65535 or 0x0000 to 0xFFFF"));
        refreshNow(); // put the value of the mock back
        return;
    }
    m_hint->clear();
    m_updating = true;
    item->setText(valueText(value)); // normalize what was typed
    m_updating = false;
    const QString device = deviceName(item->row());
    if (m_bits) {
        emit bitEdited(device, value != 0);
    } else {
        emit wordEdited(device, value);
    }
}

} // namespace mc::workbench
