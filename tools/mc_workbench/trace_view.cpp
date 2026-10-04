#include "mc_workbench/trace_view.h"

#include "mc_workbench/async_file_writer.h"
#include "mc_workbench/tab_telemetry.h"
#include "mc_workbench/trace_model.h"

#include <QCheckBox>
#include <QClipboard>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace mc::workbench {

namespace {

// Requests (TX) are drawn in bold, answers (RX) plain: no colours, so every theme works.
class TraceDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        if (index.data(TraceModel::IsTxRole).toBool()) {
            option->font.setBold(true);
        }
    }
};

// Bytes of the ring per row of the "Rows" box (the default 32 MiB for 100 000 rows).
constexpr qint64 kBytesPerRow = 336;

} // namespace

TraceView::TraceView(QWidget* parent)
    : QWidget(parent), m_table(new QTableView), m_pause(new QPushButton(QStringLiteral("Pause"))),
      m_decode(new QCheckBox(QStringLiteral("Decode"))),
      m_follow(new QCheckBox(QStringLiteral("Follow"))), m_rows(new QSpinBox),
      m_status(new QLabel), m_statusTimer(new QTimer(this)), m_scrollTimer(new QTimer(this)),
      m_writer(new AsyncFileWriter(this)) {
    m_pause->setCheckable(true);
    m_decode->setChecked(true);
    m_decode->setToolTip(QStringLiteral("Show the frame boundaries and the request / answer text"));
    m_follow->setChecked(true);
    m_rows->setRange(100, 1000000);
    m_rows->setSingleStep(10000);
    m_rows->setKeyboardTracking(false); // apply once typing is done, not on every digit
    m_rows->setValue(TraceModel::kDefaultRows);
    m_rows->setToolTip(QStringLiteral("Rows kept in the trace (older rows are dropped)"));
    auto* clear = new QPushButton(QStringLiteral("Clear"));
    auto* copy = new QPushButton(QStringLiteral("Copy"));
    auto* save = new QPushButton(QStringLiteral("Save..."));

    auto* bar = new QHBoxLayout;
    bar->addWidget(m_pause);
    bar->addWidget(clear);
    bar->addWidget(m_decode);
    bar->addWidget(m_follow);
    bar->addWidget(new QLabel(QStringLiteral("Rows")));
    bar->addWidget(m_rows);
    bar->addWidget(copy);
    bar->addWidget(save);
    bar->addStretch(1);

    m_table->setItemDelegate(new TraceDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(false);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(20);
    m_table->horizontalHeader()->setStretchLastSection(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(bar);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_status);

    m_statusTimer->setInterval(300);
    connect(m_statusTimer, &QTimer::timeout, this, &TraceView::updateStatus);
    m_scrollTimer->setSingleShot(true);
    m_scrollTimer->setInterval(0);
    connect(m_scrollTimer, &QTimer::timeout, this, [this]() {
        if (m_follow->isChecked()) {
            m_table->scrollToBottom();
        }
    });

    connect(m_pause, &QPushButton::toggled, this, [this](bool on) {
        if (m_telemetry != nullptr) {
            m_telemetry->trace()->setPaused(on);
        }
    });
    connect(clear, &QPushButton::clicked, this, [this]() {
        if (m_telemetry != nullptr) {
            m_telemetry->trace()->clear();
        }
        updateStatus();
    });
    connect(m_decode, &QCheckBox::toggled, this, [this](bool on) {
        if (m_telemetry != nullptr) {
            m_telemetry->setDecode(on);
        }
    });
    connect(m_rows, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { applyCapacity(); });
    connect(copy, &QPushButton::clicked, this, &TraceView::copyToClipboard);
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save the trace"), QStringLiteral("trace.txt"),
            QStringLiteral("Text (*.txt);;All files (*)"));
        if (!path.isEmpty()) {
            saveTo(path);
        }
    });
    connect(m_writer, &AsyncFileWriter::finished, this, &TraceView::saved);
    connect(this, &TraceView::saved, this, [this](const QString& path, bool ok, const QString& message) {
        m_status->setText(ok ? QStringLiteral("Saved %1").arg(path) : message);
    });
    updateStatus();
}

TraceView::~TraceView() = default;

void TraceView::setTelemetry(TabTelemetry* telemetry) {
    if (m_telemetry != nullptr) {
        disconnect(m_telemetry->trace(), nullptr, this, nullptr);
    }
    m_telemetry = telemetry;
    if (telemetry == nullptr) {
        m_table->setModel(nullptr);
        m_statusTimer->stop();
        updateStatus();
        return;
    }
    TraceModel* model = telemetry->trace();
    m_table->setModel(model);
    m_table->setColumnWidth(TraceModel::ColSeq, 70);
    m_table->setColumnWidth(TraceModel::ColTime, 120);
    m_table->setColumnWidth(TraceModel::ColDir, 40);
    m_table->setColumnWidth(TraceModel::ColLen, 50);
    m_table->setColumnWidth(TraceModel::ColFrame, 50);
    m_table->setColumnWidth(TraceModel::ColHex, 420);
    m_table->setColumnWidth(TraceModel::ColAscii, 150);
    connect(model, &QAbstractItemModel::rowsInserted, this, [this]() {
        if (m_follow->isChecked() && !m_scrollTimer->isActive()) {
            m_scrollTimer->start();
        }
    });
    m_pause->setChecked(model->paused());
    m_decode->setChecked(telemetry->decode());
    const QSignalBlocker block(m_rows);
    m_rows->setValue(model->capacityRows());
    m_statusTimer->start();
    updateStatus();
}

void TraceView::applyCapacity() {
    if (m_telemetry == nullptr) {
        return;
    }
    const int rows = m_rows->value();
    m_telemetry->trace()->setCapacity(rows, static_cast<qint64>(rows) * kBytesPerRow);
}

QString TraceView::copyText() const {
    if (m_telemetry == nullptr) {
        return QString();
    }
    const TraceModel* model = m_telemetry->trace();
    const QModelIndexList selected = m_table->selectionModel() != nullptr
                                         ? m_table->selectionModel()->selectedRows()
                                         : QModelIndexList();
    if (selected.isEmpty()) {
        return model->textOf(0, model->rowCount() - 1);
    }
    QList<int> rows;
    for (const QModelIndex& index : selected) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end());
    QString text;
    for (const int row : rows) {
        text += model->textOf(row, row);
    }
    return text;
}

void TraceView::copyToClipboard() {
    QGuiApplication::clipboard()->setText(copyText());
}

void TraceView::saveTo(const QString& path) {
    if (m_telemetry == nullptr) {
        emit saved(path, false, QStringLiteral("no tab is selected"));
        return;
    }
    const TraceModel* model = m_telemetry->trace();
    m_writer->write(path, model->textOf(0, model->rowCount() - 1));
}

QString TraceView::statusText() const {
    return m_status->text();
}

void TraceView::updateStatus() {
    if (m_telemetry == nullptr) {
        m_status->setText(QStringLiteral("No tab selected"));
        return;
    }
    const TraceModel* model = m_telemetry->trace();
    QString text = QStringLiteral("%1 rows of %2 chunks seen, %3 KiB held")
                       .arg(model->rowCount())
                       .arg(model->totalSeen())
                       .arg(model->memoryBytes() / 1024);
    if (model->evicted() != 0) {
        text += QStringLiteral(", %1 older rows dropped").arg(model->evicted());
    }
    if (model->droppedByRunner() != 0) {
        text += QStringLiteral(", %1 chunks left out (GUI behind)").arg(model->droppedByRunner());
    }
    if (model->skippedWhilePaused() != 0) {
        text += QStringLiteral(", %1 skipped while paused").arg(model->skippedWhilePaused());
    }
    m_status->setText(text);
}

} // namespace mc::workbench
