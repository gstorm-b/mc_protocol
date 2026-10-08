#include "mc_workbench/device_tab.h"

#include "mc_workbench/capture_export.h"
#include "mc_workbench/config_binding.h"
#include "mc_workbench/device_host.h"
#include "mc_workbench/point_table_model.h"
#include "mc_workbench/tab_telemetry.h"
#include "mc_workbench/trend_widget.h"

#include <qpb/PropertyModel.h>
#include <qpb/widgets/PropertyTreeView.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

namespace mc::workbench {

namespace {

QString linkText(mc::LinkState state) {
    switch (state) {
    case mc::LinkState::Disconnected:
        return QStringLiteral("Disconnected");
    case mc::LinkState::Connecting:
        return QStringLiteral("Connecting...");
    case mc::LinkState::Connected:
        return QStringLiteral("Connected");
    case mc::LinkState::Faulted:
        return QStringLiteral("Faulted");
    }
    return QString();
}

QString reasonText(mc::LinkReason reason) {
    switch (reason) {
    case mc::LinkReason::Requested:
        return QStringLiteral("requested");
    case mc::LinkReason::OpenFailed:
        return QStringLiteral("open failed");
    case mc::LinkReason::PeerClosed:
        return QStringLiteral("peer closed");
    case mc::LinkReason::TransportError:
        return QStringLiteral("transport error");
    case mc::LinkReason::Fault:
        return QStringLiteral("fault");
    }
    return QString();
}

QString faultKindText(int kind) {
    return kind == static_cast<int>(mc::LinkFaultKind::Timeout) ? QStringLiteral("timeout")
                                                                 : QStringLiteral("protocol error");
}

} // namespace

DeviceTab::DeviceTab(const QString& name, QWidget* parent) : QWidget(parent), m_name(name) {
    m_clock.start();
    m_host = new DeviceHost(name, mc::McDeviceConfig{}, this);
    m_binding = new ConfigBinding(mc::McDeviceConfig{}, this);
    m_points = new PointTableModel(this);
    m_console = new ConsoleModel(this);
    m_telemetry = new TabTelemetry(name, TabTelemetry::Kind::Device, this);
    m_telemetry->attachHost(m_host);
    buildUi();

    connect(m_binding, &ConfigBinding::configChanged, this,
            [this](const mc::McDeviceConfig& cfg) { sendConfig(cfg); });
    connect(m_binding, &ConfigBinding::rejected, this,
            [this](const QString&, const QString& message) {
                showMessage(QStringLiteral("Not applied: %1").arg(message), true);
            });

    connect(m_host, &DeviceHost::linkStateChanged, this, &DeviceTab::onLinkState);
    connect(m_host, &DeviceHost::linkFault, this, &DeviceTab::onFault);
    connect(m_host, &DeviceHost::valuesBatch, m_points, &PointTableModel::applyBatch);
    connect(m_host, &DeviceHost::requestFinished, m_console, &ConsoleModel::onRequestFinished);
    connect(m_host, &DeviceHost::commandDone, this, &DeviceTab::onCommandDone);
    connect(m_host, &DeviceHost::failed, this, [this](const QString& message) {
        showMessage(QStringLiteral("The device runner stopped: %1").arg(message), true);
    });

    connect(m_points, &PointTableModel::trendToggled, this, [this](const QString& key, bool on) {
        if (on) {
            m_trend->addSeries(key);
        } else {
            m_trend->removeSeries(key);
        }
    });
    connect(m_points, &PointTableModel::trendSample, this, [this](const QString& key, double value) {
        m_trend->addSample(key, m_clock.elapsed(), value);
    });

    updateButtons();
}

DeviceTab::~DeviceTab() = default;

void DeviceTab::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    // Toolbar: connect, disconnect, link state, fault.
    auto* bar = new QHBoxLayout;
    m_connectButton = new QPushButton(QStringLiteral("Connect"));
    m_disconnectButton = new QPushButton(QStringLiteral("Disconnect"));
    m_linkLabel = new QLabel(linkText(m_state));
    QFont bold = m_linkLabel->font();
    bold.setBold(true);
    m_linkLabel->setFont(bold);
    m_faultLabel = new QLabel;
    m_faultLabel->setWordWrap(false);
    bar->addWidget(m_connectButton);
    bar->addWidget(m_disconnectButton);
    bar->addWidget(m_linkLabel);
    bar->addWidget(m_faultLabel, 1);
    root->addLayout(bar);
    connect(m_connectButton, &QPushButton::clicked, this, &DeviceTab::connectToPlc);
    connect(m_disconnectButton, &QPushButton::clicked, this, &DeviceTab::disconnectFromPlc);

    auto* outer = new QSplitter(Qt::Horizontal);
    root->addWidget(outer, 1);

    // Left: the config grid and the line for refusals.
    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_grid = new qpb::PropertyTreeView;
    m_grid->setModel(m_binding->model());
    m_grid->expandAll();
    m_messageLabel = new QLabel;
    m_messageLabel->setWordWrap(true);
    leftLayout->addWidget(m_grid, 1);
    leftLayout->addWidget(m_messageLabel);
    outer->addWidget(left);

    // Right: points and trend above, console below.
    auto* right = new QSplitter(Qt::Vertical);
    outer->addWidget(right);
    outer->setStretchFactor(0, 0);
    outer->setStretchFactor(1, 1);
    outer->setSizes({380, 760});

    auto* upper = new QSplitter(Qt::Horizontal);
    right->addWidget(upper);

    auto* pointsBox = new QWidget;
    auto* pointsLayout = new QVBoxLayout(pointsBox);
    pointsLayout->setContentsMargins(0, 0, 0, 0);
    auto* subRow = new QHBoxLayout;
    m_subDevice = new QLineEdit;
    m_subDevice->setPlaceholderText(QStringLiteral("D100"));
    m_subDevice->setMaximumWidth(110);
    m_subCount = new QSpinBox;
    m_subCount->setRange(1, 100000);
    m_subCount->setValue(1);
    m_subAdd = new QPushButton(QStringLiteral("Subscribe"));
    m_subRemove = new QPushButton(QStringLiteral("Remove"));
    subRow->addWidget(new QLabel(QStringLiteral("Add:")));
    subRow->addWidget(m_subDevice);
    subRow->addWidget(new QLabel(QStringLiteral("x")));
    subRow->addWidget(m_subCount);
    subRow->addWidget(m_subAdd);
    subRow->addWidget(m_subRemove);
    subRow->addStretch(1);
    pointsLayout->addLayout(subRow);
    m_subList = new QListWidget;
    m_subList->setMaximumHeight(64);
    m_subList->setToolTip(QStringLiteral("Subscriptions added here; the configured ones are in the grid"));
    pointsLayout->addWidget(m_subList);
    m_pointView = new QTableView;
    m_pointView->setModel(m_points);
    m_pointView->setAlternatingRowColors(true);
    m_pointView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pointView->verticalHeader()->hide();
    m_pointView->verticalHeader()->setDefaultSectionSize(20);
    m_pointView->horizontalHeader()->setStretchLastSection(false);
    m_pointView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    pointsLayout->addWidget(m_pointView, 1);
    upper->addWidget(pointsBox);
    connect(m_subAdd, &QPushButton::clicked, this, &DeviceTab::onAddSubscription);
    connect(m_subRemove, &QPushButton::clicked, this, &DeviceTab::onRemoveSubscription);
    connect(m_subDevice, &QLineEdit::returnPressed, this, &DeviceTab::onAddSubscription);

    m_trend = new TrendWidget;
    upper->addWidget(m_trend);
    upper->setStretchFactor(0, 1);
    upper->setStretchFactor(1, 1);

    // Console.
    auto* consoleBox = new QWidget;
    auto* consoleLayout = new QVBoxLayout(consoleBox);
    consoleLayout->setContentsMargins(0, 0, 0, 0);
    auto* consoleRow = new QHBoxLayout;
    m_consoleOp = new QComboBox;
    m_consoleOp->addItems({QStringLiteral("Read words"), QStringLiteral("Read bits"),
                           QStringLiteral("Write words"), QStringLiteral("Write bits")});
    m_consoleHead = new QLineEdit;
    m_consoleHead->setPlaceholderText(QStringLiteral("D100"));
    m_consoleHead->setMaximumWidth(110);
    m_consoleCount = new QSpinBox;
    m_consoleCount->setRange(1, 65535);
    m_consoleCount->setValue(1);
    m_consoleValues = new QLineEdit;
    m_consoleValues->setPlaceholderText(QStringLiteral("values: 1 2 0x10"));
    m_consoleSend = new QPushButton(QStringLiteral("Send"));
    consoleRow->addWidget(m_consoleOp);
    consoleRow->addWidget(m_consoleHead);
    consoleRow->addWidget(new QLabel(QStringLiteral("x")));
    consoleRow->addWidget(m_consoleCount);
    consoleRow->addWidget(m_consoleValues, 1);
    consoleRow->addWidget(m_consoleSend);
    consoleLayout->addLayout(consoleRow);
    m_consoleError = new QLabel;
    m_consoleError->setWordWrap(true);
    consoleLayout->addWidget(m_consoleError);
    m_consoleView = new QTableView;
    m_consoleView->setModel(m_console);
    m_consoleView->verticalHeader()->hide();
    m_consoleView->verticalHeader()->setDefaultSectionSize(20);
    m_consoleView->horizontalHeader()->setStretchLastSection(true);
    m_consoleView->setSelectionBehavior(QAbstractItemView::SelectRows);
    consoleLayout->addWidget(m_consoleView, 1);
    right->addWidget(consoleBox);
    right->setStretchFactor(0, 3);
    right->setStretchFactor(1, 2);
    connect(m_consoleSend, &QPushButton::clicked, this, &DeviceTab::onSendClicked);
    connect(m_consoleHead, &QLineEdit::returnPressed, this, &DeviceTab::onSendClicked);
    connect(m_consoleValues, &QLineEdit::returnPressed, this, &DeviceTab::onSendClicked);
    const auto syncConsoleFields = [this]() {
        const bool write = m_consoleOp->currentIndex() >= 2;
        m_consoleCount->setVisible(!write);
        m_consoleValues->setVisible(write);
        m_consoleValues->setPlaceholderText(m_consoleOp->currentIndex() == 3
                                                ? QStringLiteral("bits: 1 0 1")
                                                : QStringLiteral("words: 1 2 0x10"));
    };
    connect(m_consoleOp, qOverload<int>(&QComboBox::currentIndexChanged), this, syncConsoleFields);
    syncConsoleFields();
}

QString DeviceTab::message() const {
    return m_messageLabel->text();
}

QString DeviceTab::linkStatusText() const {
    return m_linkLabel->text();
}

QString DeviceTab::faultText() const {
    return m_faultLabel->text();
}

int DeviceTab::runtimeSubscriptionCount() const {
    return m_subList->count();
}

void DeviceTab::showMessage(const QString& text, bool isError) {
    m_messageLabel->setText(text);
    m_messageLabel->setStyleSheet(isError ? QStringLiteral("color: #c62828;") : QString());
    m_messageLabel->setToolTip(text);
}

void DeviceTab::updateButtons() {
    m_connectButton->setEnabled(m_state == mc::LinkState::Disconnected);
    m_disconnectButton->setEnabled(m_state != mc::LinkState::Disconnected);
    m_binding->setEditable(m_state == mc::LinkState::Disconnected);
}

void DeviceTab::sendConfig(const mc::McDeviceConfig& cfg) {
    // A loopback address cannot be a real PLC: a capture of it defaults to the mock source.
    m_telemetry->setSuggestedSource(cfg.transport == mc::TransportKind::Tcp && isLoopbackHost(cfg.tcp.host)
                                        ? CaptureSource::MockPlc
                                        : CaptureSource::RealPlc);
    m_points->setXyNotation(cfg.frame.xyNotation);
    m_configTokens.insert(m_host->applyConfig(cfg));
}

bool DeviceTab::setConfig(const mc::McDeviceConfig& cfg, QString* error) {
    if (m_state != mc::LinkState::Disconnected) {
        if (error != nullptr) {
            *error = QStringLiteral("disconnect before changing the configuration");
        }
        return false;
    }
    QString message;
    if (!m_binding->setConfig(cfg, &message)) {
        if (error != nullptr) {
            *error = message;
        }
        showMessage(QStringLiteral("Not applied: %1").arg(message), true);
        return false;
    }
    sendConfig(cfg);
    return true;
}

void DeviceTab::connectToPlc() {
    m_connectButton->setEnabled(false);
    m_faultLabel->clear();
    m_host->connectToPlc();
}

void DeviceTab::disconnectFromPlc() {
    m_disconnectButton->setEnabled(false);
    m_host->disconnectFromPlc();
}

void DeviceTab::onLinkState(mc::LinkState state, mc::LinkReason reason, const QString& detail) {
    m_state = state;
    QString text = linkText(state);
    if (state == mc::LinkState::Disconnected && reason != mc::LinkReason::Requested) {
        text += QStringLiteral(" (%1)").arg(reasonText(reason));
    }
    if (!detail.isEmpty() && state != mc::LinkState::Connected) {
        text += QStringLiteral(" - %1").arg(detail);
    }
    m_linkLabel->setText(text);
    m_telemetry->note(state == mc::LinkState::Faulted ? mc::LogLevel::Error : mc::LogLevel::Info,
                      QStringLiteral("workbench.link"), text);
    if (state == mc::LinkState::Connecting) {
        m_faultLabel->clear();
    }
    if (state == mc::LinkState::Disconnected) {
        m_points->markStale();
    }
    updateButtons();
    emit linkStateChanged(state);
}

void DeviceTab::onFault(const FaultReport& fault) {
    m_telemetry->note(mc::LogLevel::Error, QStringLiteral("workbench.fault"),
                      QStringLiteral("%1: %2").arg(faultKindText(fault.kind), fault.message));
    m_faultLabel->setText(QStringLiteral("Fault: %1 - %2%3")
                              .arg(faultKindText(fault.kind), fault.message,
                                   fault.reopenTransport ? QStringLiteral(" (reconnect needed)") : QString()));
}

void DeviceTab::onCommandDone(const CommandResult& result) {
    m_console->onCommandDone(result);

    if (m_configTokens.remove(result.token)) {
        if (result.ok) {
            m_points->clear();
            m_subList->clear();
            showMessage(QString(), false);
        } else {
            showMessage(QStringLiteral("The device did not take the configuration: %1")
                            .arg(result.message),
                        true);
        }
        emit configApplied(result.ok, result.message);
        return;
    }

    const auto sub = m_pendingSubscribe.find(result.token);
    if (sub != m_pendingSubscribe.end()) {
        if (result.ok) {
            auto* item = new QListWidgetItem(QStringLiteral("%1 x%2  (id %3)")
                                                 .arg(sub->device)
                                                 .arg(sub->count)
                                                 .arg(result.value));
            item->setData(Qt::UserRole, static_cast<quint32>(result.value));
            m_subList->addItem(item);
            showMessage(QString(), false);
        } else {
            showMessage(QStringLiteral("Subscribe %1 failed: %2").arg(sub->device, result.message), true);
        }
        m_pendingSubscribe.erase(sub);
        return;
    }

    const auto unsub = m_pendingUnsubscribe.find(result.token);
    if (unsub != m_pendingUnsubscribe.end()) {
        if (result.ok) {
            for (int i = 0; i < m_subList->count(); ++i) {
                if (m_subList->item(i)->data(Qt::UserRole).toUInt() == unsub.value()) {
                    delete m_subList->takeItem(i);
                    break;
                }
            }
        } else {
            showMessage(QStringLiteral("Remove failed: %1").arg(result.message), true);
        }
        m_pendingUnsubscribe.erase(unsub);
    }
}

quint64 DeviceTab::subscribe(const QString& device, quint32 count) {
    const quint64 token = m_host->subscribe(device, count);
    m_pendingSubscribe.insert(token, PendingSubscription{device.trimmed(), count});
    return token;
}

void DeviceTab::onAddSubscription() {
    const QString device = m_subDevice->text().trimmed();
    if (device.isEmpty()) {
        showMessage(QStringLiteral("Type a device to subscribe, for example D100"), true);
        return;
    }
    subscribe(device, static_cast<quint32>(m_subCount->value()));
}

void DeviceTab::onRemoveSubscription() {
    const QListWidgetItem* item = m_subList->currentItem();
    if (item == nullptr) {
        return;
    }
    const quint32 id = item->data(Qt::UserRole).toUInt();
    const quint64 token = m_host->unsubscribe(id);
    m_pendingUnsubscribe.insert(token, id);
}

quint64 DeviceTab::sendRead(bool bits, const QString& head, quint32 count, QString* error) {
    const QString device = head.trimmed();
    QString problem;
    if (device.isEmpty()) {
        problem = QStringLiteral("type the head device, for example D100");
    } else if (count < 1 || count > 65535) {
        problem = QStringLiteral("the count must be 1 to 65535");
    }
    if (!problem.isEmpty()) {
        if (error != nullptr) {
            *error = problem;
        }
        m_consoleError->setText(problem);
        return 0;
    }
    m_consoleError->clear();
    const quint64 token = bits ? m_host->readBits(device, static_cast<quint16>(count))
                               : m_host->readWords(device, static_cast<quint16>(count));
    m_console->begin(token, bits ? ConsoleOp::ReadBits : ConsoleOp::ReadWords,
                     QStringLiteral("read %1 %2 x%3")
                         .arg(bits ? QStringLiteral("bits") : QStringLiteral("words"), device)
                         .arg(count));
    return token;
}

quint64 DeviceTab::sendWrite(bool bits, const QString& head, const QString& valuesText,
                             QString* error) {
    const QString device = head.trimmed();
    QString problem;
    QVector<quint16> words;
    QVector<bool> bitValues;
    if (device.isEmpty()) {
        problem = QStringLiteral("type the head device, for example D100");
    } else if (bits) {
        parseBitList(valuesText, bitValues, &problem);
    } else {
        parseWordList(valuesText, words, &problem);
    }
    if (!problem.isEmpty()) {
        if (error != nullptr) {
            *error = problem;
        }
        m_consoleError->setText(problem);
        return 0;
    }
    m_consoleError->clear();
    QStringList shown;
    quint64 token = 0;
    if (bits) {
        for (const bool bit : bitValues) {
            shown.push_back(bit ? QStringLiteral("1") : QStringLiteral("0"));
        }
        token = m_host->writeBits(device, bitValues);
    } else {
        for (const quint16 word : words) {
            shown.push_back(QString::number(word));
        }
        token = m_host->writeWords(device, words);
    }
    m_console->begin(token, bits ? ConsoleOp::WriteBits : ConsoleOp::WriteWords,
                     QStringLiteral("write %1 %2 = %3")
                         .arg(bits ? QStringLiteral("bits") : QStringLiteral("words"), device,
                              shown.join(QLatin1Char(' '))));
    return token;
}

void DeviceTab::onSendClicked() {
    const int op = m_consoleOp->currentIndex();
    const QString head = m_consoleHead->text();
    if (op < 2) {
        sendRead(op == 1, head, static_cast<quint32>(m_consoleCount->value()));
    } else {
        sendWrite(op == 3, head, m_consoleValues->text());
    }
    m_consoleView->scrollToBottom();
}

DevicePane::DevicePane(QWidget* parent)
    : QWidget(parent), m_tabs(new QTabWidget), m_empty(new QLabel) {
    m_tabs->setTabsClosable(true);
    m_tabs->setDocumentMode(true);
    m_empty->setText(QStringLiteral("No device yet. Press \"New device\"."));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setEnabled(false);

    auto* add = new QPushButton(QStringLiteral("New device"));
    auto* bar = new QHBoxLayout;
    bar->addWidget(add);
    bar->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(m_empty, 1);
    layout->addWidget(m_tabs, 1);
    m_tabs->setVisible(false);

    connect(add, &QPushButton::clicked, this, [this]() { addDevice(); });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) { closeDevice(index); });
}

DeviceTab* DevicePane::addDevice() {
    auto* tab = new DeviceTab(QStringLiteral("PLC %1").arg(++m_serial));
    adoptDevice(tab);
    return tab;
}

void DevicePane::adoptDevice(DeviceTab* tab) {
    if (m_registry != nullptr) {
        m_registry->add(tab->telemetry());
    }
    const int index = m_tabs->addTab(tab, tab->name());
    m_tabs->setCurrentIndex(index);
    m_tabs->setVisible(true);
    m_empty->setVisible(false);
}

int DevicePane::currentIndex() const {
    return m_tabs->currentIndex();
}

void DevicePane::setCurrentIndex(int index) {
    if (index >= 0 && index < m_tabs->count()) {
        m_tabs->setCurrentIndex(index);
    }
}

void DevicePane::setRegistry(TelemetryRegistry* registry) {
    m_registry = registry;
    if (m_registry == nullptr) {
        return;
    }
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (DeviceTab* tab = deviceAt(i)) {
            m_registry->add(tab->telemetry());
        }
    }
}

int DevicePane::deviceCount() const {
    return m_tabs->count();
}

DeviceTab* DevicePane::deviceAt(int index) const {
    return qobject_cast<DeviceTab*>(m_tabs->widget(index));
}

void DevicePane::closeDevice(int index) {
    DeviceTab* tab = deviceAt(index);
    if (tab == nullptr) {
        return;
    }
    m_tabs->removeTab(index);
    delete tab; // stops the runner thread within its bound
    if (m_tabs->count() == 0) {
        m_tabs->setVisible(false);
        m_empty->setVisible(true);
    }
}

} // namespace mc::workbench
