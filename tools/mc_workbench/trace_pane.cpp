#include "mc_workbench/trace_pane.h"

#include "mc_workbench/capture_panel.h"
#include "mc_workbench/tab_telemetry.h"
#include "mc_workbench/trace_view.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

namespace mc::workbench {

TracePane::TracePane(TelemetryRegistry* registry, QWidget* parent)
    : QWidget(parent), m_registry(registry), m_tabs(new QComboBox), m_trace(new TraceView),
      m_capture(new CapturePanel) {
    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(QStringLiteral("Tab")));
    bar->addWidget(m_tabs);
    bar->addStretch(1);

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(m_trace);
    split->addWidget(m_capture);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(bar);
    layout->addWidget(split, 1);

    connect(m_tabs, qOverload<int>(&QComboBox::currentIndexChanged), this, &TracePane::onSelected);
    connect(registry, &TelemetryRegistry::tabAdded, this, [this]() { refill(); });
    connect(registry, &TelemetryRegistry::tabRemoved, this, [this](TabTelemetry* gone) {
        // The telemetry is being destroyed: let go of it before anything uses it again.
        if (m_trace->telemetry() == gone) {
            m_trace->setTelemetry(nullptr);
            m_capture->setTelemetry(nullptr);
        }
        refill();
    });
    refill();
}

TabTelemetry* TracePane::current() const {
    return m_trace->telemetry();
}

void TracePane::select(TabTelemetry* telemetry) {
    const int index = m_tabs->findData(QVariant::fromValue(reinterpret_cast<quintptr>(telemetry)));
    if (index >= 0) {
        m_tabs->setCurrentIndex(index);
    }
}

void TracePane::refill() {
    const QPointer<TabTelemetry> shown = m_trace->telemetry();
    const QSignalBlocker block(m_tabs);
    m_tabs->clear();
    for (TabTelemetry* tab : m_registry->tabs()) {
        m_tabs->addItem(tab->name(), QVariant::fromValue(reinterpret_cast<quintptr>(tab)));
    }
    int index = -1;
    if (!shown.isNull()) {
        index = m_tabs->findData(QVariant::fromValue(reinterpret_cast<quintptr>(shown.data())));
    }
    if (index < 0 && m_tabs->count() > 0) {
        index = 0;
    }
    m_tabs->setCurrentIndex(index);
    // The blocker hid the change from onSelected(): apply it here.
    TabTelemetry* wanted = nullptr;
    if (index >= 0) {
        wanted = reinterpret_cast<TabTelemetry*>(m_tabs->itemData(index).value<quintptr>());
    }
    if (wanted != m_trace->telemetry()) {
        m_trace->setTelemetry(wanted);
        m_capture->setTelemetry(wanted);
    }
}

void TracePane::onSelected(int index) {
    TabTelemetry* wanted = nullptr;
    if (index >= 0) {
        wanted = reinterpret_cast<TabTelemetry*>(m_tabs->itemData(index).value<quintptr>());
    }
    m_trace->setTelemetry(wanted);
    m_capture->setTelemetry(wanted);
}

} // namespace mc::workbench
