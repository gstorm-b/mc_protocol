#include "mc_workbench/log_view.h"

#include "mc_workbench/async_file_writer.h"
#include "mc_workbench/log_model.h"

#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

namespace mc::workbench {

namespace {

const mc::LogLevel kLevels[] = {mc::LogLevel::Trace, mc::LogLevel::Debug, mc::LogLevel::Info,
                                mc::LogLevel::Warn, mc::LogLevel::Error};

} // namespace

LogView::LogView(LogModel* model, QWidget* parent)
    : QWidget(parent), m_model(model), m_filter(new LogFilterModel(this)), m_table(new QTableView),
      m_level(new QComboBox), m_tab(new QComboBox), m_search(new QLineEdit), m_rows(new QSpinBox),
      m_status(new QLabel), m_writer(new AsyncFileWriter(this)) {
    m_filter->setSourceModel(model);
    for (const mc::LogLevel level : kLevels) {
        m_level->addItem(LogModel::levelName(level), static_cast<int>(level));
    }
    m_level->setToolTip(QStringLiteral("Lowest level shown"));
    m_tab->addItem(QStringLiteral("All tabs"), QString());
    m_tab->setToolTip(QStringLiteral("Show the lines of one tab"));
    m_search->setPlaceholderText(QStringLiteral("Search"));
    m_search->setClearButtonEnabled(true);
    m_rows->setRange(100, 1000000);
    m_rows->setSingleStep(10000);
    m_rows->setKeyboardTracking(false);
    m_rows->setValue(model->capacityRows());
    m_rows->setToolTip(QStringLiteral("Lines kept in the log (older lines are dropped)"));
    auto* clear = new QPushButton(QStringLiteral("Clear"));
    auto* copy = new QPushButton(QStringLiteral("Copy"));
    auto* save = new QPushButton(QStringLiteral("Save..."));

    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(QStringLiteral("Level")));
    bar->addWidget(m_level);
    bar->addWidget(new QLabel(QStringLiteral("Tab")));
    bar->addWidget(m_tab);
    bar->addWidget(m_search, 1);
    bar->addWidget(new QLabel(QStringLiteral("Rows")));
    bar->addWidget(m_rows);
    bar->addWidget(clear);
    bar->addWidget(copy);
    bar->addWidget(save);

    m_table->setModel(m_filter);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(false);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(20);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(LogModel::ColSeq, 70);
    m_table->setColumnWidth(LogModel::ColTime, 120);
    m_table->setColumnWidth(LogModel::ColLevel, 50);
    m_table->setColumnWidth(LogModel::ColTab, 80);
    m_table->setColumnWidth(LogModel::ColCategory, 110);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(bar);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_status);

    refillTabs();
    connect(model, &LogModel::tabAdded, this, [this]() { refillTabs(); });
    connect(m_level, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_filter->setMinLevel(static_cast<mc::LogLevel>(m_level->itemData(index).toInt()));
        updateStatus();
    });
    connect(m_tab, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_filter->setTab(m_tab->itemData(index).toString());
        updateStatus();
    });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_filter->setSearch(text);
        updateStatus();
    });
    connect(m_rows, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int rows) { m_model->setCapacity(rows); });
    connect(clear, &QPushButton::clicked, this, [this]() {
        m_model->clear();
        updateStatus();
    });
    connect(copy, &QPushButton::clicked, this, &LogView::copyToClipboard);
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save the debug log"), QStringLiteral("debug-log.txt"),
            QStringLiteral("Text (*.txt);;All files (*)"));
        if (!path.isEmpty()) {
            saveTo(path);
        }
    });
    connect(m_writer, &AsyncFileWriter::finished, this, &LogView::saved);
    connect(this, &LogView::saved, this, [this](const QString& path, bool ok, const QString& message) {
        m_status->setText(ok ? QStringLiteral("Saved %1").arg(path) : message);
    });
    connect(m_filter, &QAbstractItemModel::rowsInserted, this, [this]() {
        m_table->scrollToBottom();
        updateStatus();
    });
    updateStatus();
}

LogView::~LogView() = default;

void LogView::refillTabs() {
    const QSignalBlocker block(m_tab);
    const QString current = m_tab->currentData().toString();
    m_tab->clear();
    m_tab->addItem(QStringLiteral("All tabs"), QString());
    for (const QString& tab : m_model->tabs()) {
        m_tab->addItem(tab, tab);
    }
    const int index = m_tab->findData(current);
    m_tab->setCurrentIndex(index < 0 ? 0 : index);
}

QString LogView::copyText() const {
    const QModelIndexList selected = m_table->selectionModel() != nullptr
                                         ? m_table->selectionModel()->selectedRows()
                                         : QModelIndexList();
    if (selected.isEmpty()) {
        return m_filter->shownText();
    }
    QList<int> rows;
    for (const QModelIndex& index : selected) {
        rows.append(m_filter->mapToSource(index).row());
    }
    std::sort(rows.begin(), rows.end());
    QString text;
    for (const int row : rows) {
        text += m_model->lineText(row) + QLatin1Char('\n');
    }
    return text;
}

void LogView::copyToClipboard() {
    QGuiApplication::clipboard()->setText(copyText());
}

void LogView::saveTo(const QString& path) {
    m_writer->write(path, m_filter->shownText());
}

void LogView::setLevelFilter(const QString& levelName) {
    const int index = m_level->findText(levelName);
    if (index >= 0) {
        m_level->setCurrentIndex(index);
    }
}

void LogView::setTabFilter(const QString& tab) {
    const int index = m_tab->findData(tab);
    m_tab->setCurrentIndex(index < 0 ? 0 : index);
}

void LogView::setSearchText(const QString& text) {
    m_search->setText(text);
}

void LogView::updateStatus() {
    m_status->setText(QStringLiteral("%1 of %2 lines shown, %3 dropped from the ring")
                          .arg(m_filter->rowCount())
                          .arg(m_model->rowCount())
                          .arg(m_model->evicted()));
}

} // namespace mc::workbench
