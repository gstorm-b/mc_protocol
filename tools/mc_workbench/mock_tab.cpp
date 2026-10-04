#include "mc_workbench/mock_tab.h"

#include "mc_workbench/mock_fault_panel.h"
#include "mc_workbench/mock_host.h"
#include "mc_workbench/mock_memory_editor.h"
#include "mc_workbench/mock_request_log_model.h"
#include "mc_workbench/tab_telemetry.h"

#include "mc/core/request.h"

#include <qpb/PropertyGroup.h>
#include <qpb/PropertyModel.h>
#include <qpb/Serialization.h>
#include <qpb/widgets/PropertyTreeView.h>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

namespace mc::workbench {

namespace {

// Paths of the settings grid.
constexpr const char* kFrameType = "Frame/frame";
constexpr const char* kCode = "Frame/code";
constexpr const char* kSeries = "Frame/series";
constexpr const char* kNetwork = "Frame/network";
constexpr const char* kPc = "Frame/pc";
constexpr const char* kIo = "Frame/io";
constexpr const char* kStation = "Frame/station";
constexpr const char* kFormat = "Frame/format";
constexpr const char* kStationNo = "Frame/stationNo";
constexpr const char* kSelfStation = "Frame/selfStation";
constexpr const char* kSumCheck = "Frame/sumCheck";
constexpr const char* kBlockNo = "Frame/blockNo";
constexpr const char* kCheckBlockNo = "Frame/checkBlockNo";
constexpr const char* kSendEot = "Frame/sendEotOnError";
constexpr const char* kF3Sum = "Frame/f3ShortResponseHasSum";
constexpr const char* kMessageWait = "Frame/messageWait";
constexpr const char* kCommandSet = "Frame/commandSet";
constexpr const char* kTargetFamily = "Frame/targetFamily";
constexpr const char* kAliasLS = "Frame/e1AliasLS";
constexpr const char* kASeries = "Frame/aSeriesTarget";
constexpr const char* kXyNotation = "Frame/xyNotation";
constexpr const char* kXyAscii = "Frame/xyAsciiDigits";

constexpr const char* kMode = "Serving/mode";
constexpr const char* kTcpPort = "Serving/tcpPort";
constexpr const char* kComPort = "Serving/comPort";
constexpr const char* kBaud = "Serving/baudRate";
constexpr const char* kDataBits = "Serving/dataBits";
constexpr const char* kParity = "Serving/parity";
constexpr const char* kStopBits = "Serving/stopBits";
constexpr const char* kLogLevel = "Serving/logLevel";

constexpr const char* kUnsupportedQna = "Errors/unsupportedQna";
constexpr const char* kUnsupported1e = "Errors/unsupported1e";
constexpr const char* kUnsupported1c = "Errors/unsupported1c";
constexpr const char* kSumErrorQna = "Errors/sumErrorQna";
constexpr const char* kSumError1c = "Errors/sumError1c";
constexpr const char* kOutOfRangeQna = "Errors/outOfRangeQna";
constexpr const char* kOutOfRange1e = "Errors/outOfRange1e";
constexpr const char* kOutOfRange1eAbn = "Errors/outOfRange1eAbnormal";
constexpr const char* kOutOfRange1c = "Errors/outOfRange1c";

constexpr int kMaxLogBlocks = 5000;

bool parseHex(const QString& text, quint32 maximum, quint32& out) {
    QString t = text.trimmed();
    if (t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        t = t.mid(2);
    }
    bool ok = false;
    const qulonglong value = t.toULongLong(&ok, 16);
    if (!ok || value > maximum) {
        return false;
    }
    out = static_cast<quint32>(value);
    return true;
}

QString hexText(quint32 value, int digits) {
    return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

// A hex text property of at most `bits` bits.
void addHex(qpb::PropertyGroup& group, const char* id, const QString& title, quint32 value,
            int bits, const QString& tip) {
    const quint32 maximum = bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    group.addString(QString::fromLatin1(id), hexText(value, bits / 4))
        .displayName(title)
        .toolTip(tip + QStringLiteral(" (hexadecimal, %1 bits)").arg(bits))
        .validator([maximum](const QVariant& v, const qpb::Property&) {
            quint32 parsed = 0;
            return parseHex(v.toString(), maximum, parsed)
                       ? qpb::ValidationResult::valid()
                       : qpb::ValidationResult::error(QStringLiteral("a hexadecimal number up to %1")
                                                          .arg(hexText(maximum, 1)));
        });
}

const char* levelName(mc::LogLevel level) {
    switch (level) {
    case mc::LogLevel::Trace:
        return "TRACE";
    case mc::LogLevel::Debug:
        return "DEBUG";
    case mc::LogLevel::Info:
        return "INFO ";
    case mc::LogLevel::Warn:
        return "WARN ";
    case mc::LogLevel::Error:
        return "ERROR";
    case mc::LogLevel::Off:
        return "OFF  ";
    }
    return "?    ";
}

} // namespace

MockTab::MockTab(const QString& name, QWidget* parent) : QWidget(parent), m_name(name) {
    registerMockMetaTypes();
    setObjectName(name);
    buildSettings();
    m_host = new MockHost(name, frameConfig(), mockSettings());
    m_telemetry = new TabTelemetry(name, TabTelemetry::Kind::Mock, this);
    m_telemetry->setSuggestedSource(CaptureSource::MockPlc);
    m_telemetry->attachHost(m_host);
    buildUi();
    connectHost();
    m_memory->setAutoRefresh(true);
    updateButtons();
}

MockTab::~MockTab() {
    // Join the runner thread first, while the widgets that receive its signals still exist.
    delete m_host;
    m_host = nullptr;
}

void MockTab::buildSettings() {
    auto root = qpb::PropertyGroup::create(QStringLiteral("Mock"));

    auto& frame = root->addGroup(QStringLiteral("Frame"));
    frame.addEnum(QStringLiteral("frame"), QStringList{"3E", "1E", "3C", "1C"}, 0)
        .displayName(QStringLiteral("Frame"))
        .toolTip(QStringLiteral("Frame family the mock answers"));
    frame.addEnum(QStringLiteral("code"), QStringList{"Binary", "ASCII"}, 0)
        .displayName(QStringLiteral("Data code"))
        .toolTip(QStringLiteral("3E and 1E only; the serial frames are ASCII"))
        .enabledWhen(QString::fromLatin1(kFrameType),
                     [](const QVariant& v) { return v.toInt() < 2; });
    frame.addEnum(QStringLiteral("series"), QStringList{"Q/L", "iQ-R"}, 0)
        .displayName(QStringLiteral("PLC series"))
        .toolTip(QStringLiteral("Device code column of the QnA frames (3E, 3C)"));
    frame.addInt(QStringLiteral("network"), 0).range(0, 255).displayName(QStringLiteral("Network No."));
    frame.addInt(QStringLiteral("pc"), 255).range(0, 255).displayName(QStringLiteral("PC No."));
    frame.addInt(QStringLiteral("io"), 0x03FF)
        .range(0, 0xFFFF)
        .displayName(QStringLiteral("Module I/O No."))
        .toolTip(QStringLiteral("3E only; decimal (0x03FF = 1023)"))
        .visibleWhen(QString::fromLatin1(kFrameType), 0);
    frame.addInt(QStringLiteral("station"), 0)
        .range(0, 255)
        .displayName(QStringLiteral("Module station No."))
        .visibleWhen(QString::fromLatin1(kFrameType), 0);
    const auto serialOnly = [](const QVariant& v) { return v.toInt() >= 2; };
    frame.addEnum(QStringLiteral("format"), QStringList{"1", "2", "3", "4"}, 0)
        .displayName(QStringLiteral("Serial format"))
        .visibleWhen(QString::fromLatin1(kFrameType), serialOnly);
    frame.addInt(QStringLiteral("stationNo"), 0)
        .range(0, 255)
        .displayName(QStringLiteral("Station No."))
        .toolTip(QStringLiteral("The station the mock answers for (3C, 1C)"))
        .visibleWhen(QString::fromLatin1(kFrameType), serialOnly);
    frame.addInt(QStringLiteral("selfStation"), 0)
        .range(0, 255)
        .displayName(QStringLiteral("Self-station No."))
        .visibleWhen(QString::fromLatin1(kFrameType), serialOnly);
    frame.addBool(QStringLiteral("sumCheck"), true)
        .displayName(QStringLiteral("Sum check"))
        .visibleWhen(QString::fromLatin1(kFrameType), serialOnly);
    frame.addInt(QStringLiteral("blockNo"), 0)
        .range(0, 255)
        .displayName(QStringLiteral("Block No."))
        .toolTip(QStringLiteral("Format 2 only"))
        .visibleWhen(QString::fromLatin1(kFrameType), serialOnly);
    frame.addBool(QStringLiteral("checkBlockNo"), true)
        .displayName(QStringLiteral("Check block No."))
        .visibleWhen(QString::fromLatin1(kFrameType), serialOnly);
    frame.addBool(QStringLiteral("sendEotOnError"), true)
        .displayName(QStringLiteral("Send EOT on error"))
        .visibleWhen(QString::fromLatin1(kFrameType), serialOnly);
    frame.addBool(QStringLiteral("f3ShortResponseHasSum"), false)
        .displayName(QStringLiteral("Format 3 short response has SUM"))
        .visibleWhen(QString::fromLatin1(kFrameType), serialOnly);
    frame.addInt(QStringLiteral("messageWait"), 0)
        .range(0, 15)
        .displayName(QStringLiteral("Message wait (x10 ms)"))
        .toolTip(QStringLiteral("1C only"))
        .visibleWhen(QString::fromLatin1(kFrameType), serialOnly);
    frame.addEnum(QStringLiteral("commandSet"), QStringList{"ACPU", "AnA/AnU"}, 0)
        .displayName(QStringLiteral("1C command set"))
        .visibleWhen(QString::fromLatin1(kFrameType), serialOnly);
    frame.addEnum(QStringLiteral("targetFamily"), QStringList{"iQ-R / Q / L", "QnA", "A"}, 0)
        .displayName(QStringLiteral("Target family"))
        .toolTip(QStringLiteral("Point-limit column of the protocol"));
    frame.addBool(QStringLiteral("e1AliasLS"), false)
        .displayName(QStringLiteral("1E: L and S as M"))
        .visibleWhen(QString::fromLatin1(kFrameType), 1);
    frame.addBool(QStringLiteral("aSeriesTarget"), false)
        .displayName(QStringLiteral("A-series access rule"));
    frame.addEnum(QStringLiteral("xyNotation"), QStringList{"Hex (Q, L, iQ-R)", "Octal (FX3, FX5)"}, 0)
        .displayName(QStringLiteral("X / Y numbering in text"))
        .toolTip(QStringLiteral("Base of X and Y numbers in device text and in the editor"));
    frame.addEnum(QStringLiteral("xyAsciiDigits"), QStringList{"Hex", "Octal"}, 0)
        .displayName(QStringLiteral("X / Y digits in ASCII frames"));

    auto& errors = root->addGroup(QStringLiteral("Errors"));
    const MockSettings defaults;
    addHex(errors, "unsupportedQna", QStringLiteral("Unsupported (3E, 3C)"), defaults.unsupportedQna, 16,
           QStringLiteral("End code for a command or subcommand the mock does not support"));
    addHex(errors, "unsupported1e", QStringLiteral("Unsupported (1E)"), defaults.unsupported1e, 8,
           QStringLiteral("1E end code for an unsupported command"));
    addHex(errors, "unsupported1c", QStringLiteral("Unsupported (1C)"), defaults.unsupported1c, 8,
           QStringLiteral("1C NAK code for an unsupported command"));
    addHex(errors, "sumErrorQna", QStringLiteral("Wrong SUM (3C)"), defaults.sumErrorQna, 16,
           QStringLiteral("End code for a 3C request with a wrong SUM"));
    addHex(errors, "sumError1c", QStringLiteral("Wrong SUM (1C)"), defaults.sumError1c, 8,
           QStringLiteral("1C NAK code for a request with a wrong SUM"));
    addHex(errors, "outOfRangeQna", QStringLiteral("Out of range (3E, 3C)"), defaults.outOfRangeQna, 16,
           QStringLiteral("End code for a device beyond the limit"));
    addHex(errors, "outOfRange1e", QStringLiteral("Out of range (1E)"), defaults.outOfRange1e, 8,
           QStringLiteral("1E end code for a device beyond the limit"));
    addHex(errors, "outOfRange1eAbnormal", QStringLiteral("Out of range (1E abnormal)"),
           defaults.outOfRange1eAbnormal, 8,
           QStringLiteral("1E abnormal code sent with the out-of-range end code"));
    addHex(errors, "outOfRange1c", QStringLiteral("Out of range (1C)"), defaults.outOfRange1c, 8,
           QStringLiteral("1C NAK code for a device beyond the limit"));

    auto& serving = root->addGroup(QStringLiteral("Serving"));
    serving.addEnum(QStringLiteral("mode"), QStringList{"TCP (127.0.0.1)", "COM port"}, 0)
        .displayName(QStringLiteral("Serve over"));
    serving.addInt(QStringLiteral("tcpPort"), 0)
        .range(0, 65535)
        .displayName(QStringLiteral("TCP port"))
        .toolTip(QStringLiteral("0 lets the system choose a free port"))
        .visibleWhen(QString::fromLatin1(kMode), 0);
    serving.addString(QStringLiteral("comPort"), QStringLiteral("COM1"))
        .displayName(QStringLiteral("COM port"))
        .visibleWhen(QString::fromLatin1(kMode), 1);
    serving.addInt(QStringLiteral("baudRate"), 9600)
        .range(50, 4000000)
        .displayName(QStringLiteral("Baud rate"))
        .visibleWhen(QString::fromLatin1(kMode), 1);
    serving.addEnum(QStringLiteral("dataBits"), QStringList{"7", "8"}, 0)
        .displayName(QStringLiteral("Data bits"))
        .visibleWhen(QString::fromLatin1(kMode), 1);
    serving.addEnum(QStringLiteral("parity"), QStringList{"None", "Even", "Odd"}, 1)
        .displayName(QStringLiteral("Parity"))
        .visibleWhen(QString::fromLatin1(kMode), 1);
    serving.addEnum(QStringLiteral("stopBits"), QStringList{"1", "2"}, 0)
        .displayName(QStringLiteral("Stop bits"))
        .visibleWhen(QString::fromLatin1(kMode), 1);
    serving.addEnum(QStringLiteral("logLevel"),
                    QStringList{"Trace", "Debug", "Info", "Warn", "Error", "Off"}, 2)
        .displayName(QStringLiteral("Log level"))
        .toolTip(QStringLiteral("Lowest level of the mock log lines that are kept; applies at once"));

    m_model = new qpb::PropertyModel(std::move(root), this);

    // Frame and error code changes rebuild the mock when serving starts; endpoint changes do not.
    const auto markDirty = [this](const QString&, const QVariant&) {
        m_shapeDirty = true;
        if (m_serving) {
            m_message = QStringLiteral("stop and start again to apply the changed settings");
            emit statusChanged(statusText());
            if (m_statusLabel != nullptr) {
                m_statusLabel->setText(statusText());
            }
        }
    };
    m_model->onValueChanged(QStringLiteral("Frame"), this, markDirty);
    m_model->onValueChanged(QStringLiteral("Errors"), this, markDirty);
}

namespace {

int intAt(const qpb::PropertyModel* model, const char* path) {
    const qpb::Property* property = model->find(QString::fromLatin1(path));
    return property != nullptr ? property->value().toInt() : 0;
}

bool boolAt(const qpb::PropertyModel* model, const char* path) {
    const qpb::Property* property = model->find(QString::fromLatin1(path));
    return property != nullptr && property->value().toBool();
}

QString textAt(const qpb::PropertyModel* model, const char* path) {
    const qpb::Property* property = model->find(QString::fromLatin1(path));
    return property != nullptr ? property->value().toString() : QString();
}

quint32 hexAt(const qpb::PropertyModel* model, const char* path, quint32 fallback) {
    quint32 value = fallback;
    if (!parseHex(textAt(model, path), 0xFFFFFFFFu, value)) {
        return fallback;
    }
    return value;
}

} // namespace

mc::FrameConfig MockTab::frameConfig() const {
    const mc::DataCode code =
        intAt(m_model, kCode) == 1 ? mc::DataCode::Ascii : mc::DataCode::Binary;
    const auto format = static_cast<mc::SerialFormat>(intAt(m_model, kFormat) + 1);
    mc::FrameConfig c;
    switch (intAt(m_model, kFrameType)) {
    case 1:
        c = mc::FrameConfig::frame1E(code);
        break;
    case 2:
        c = mc::FrameConfig::frame3C(format);
        break;
    case 3:
        c = mc::FrameConfig::frame1C(format);
        break;
    default:
        c = mc::FrameConfig::frame3E(code);
        break;
    }
    c.series = intAt(m_model, kSeries) == 1 ? mc::PlcSeries::IqR : mc::PlcSeries::QL;
    c.network = static_cast<uint8_t>(intAt(m_model, kNetwork));
    c.pc = static_cast<uint8_t>(intAt(m_model, kPc));
    c.io = static_cast<uint16_t>(intAt(m_model, kIo));
    c.station = static_cast<uint8_t>(intAt(m_model, kStation));
    c.stationNo = static_cast<uint8_t>(intAt(m_model, kStationNo));
    c.selfStation = static_cast<uint8_t>(intAt(m_model, kSelfStation));
    c.sumCheck = boolAt(m_model, kSumCheck);
    c.blockNo = static_cast<uint8_t>(intAt(m_model, kBlockNo));
    c.checkBlockNo = boolAt(m_model, kCheckBlockNo);
    c.sendEotOnError = boolAt(m_model, kSendEot);
    c.f3ShortResponseHasSum = boolAt(m_model, kF3Sum);
    c.messageWait = static_cast<uint8_t>(intAt(m_model, kMessageWait));
    c.commandSet = intAt(m_model, kCommandSet) == 1 ? mc::C1CommandSet::AnA : mc::C1CommandSet::ACPU;
    switch (intAt(m_model, kTargetFamily)) {
    case 1:
        c.targetFamily = mc::TargetFamily::QnA;
        break;
    case 2:
        c.targetFamily = mc::TargetFamily::A;
        break;
    default:
        c.targetFamily = mc::TargetFamily::IqR_Q_L;
        break;
    }
    c.e1AliasLS = boolAt(m_model, kAliasLS);
    c.aSeriesTarget = boolAt(m_model, kASeries);
    c.xyNotation = intAt(m_model, kXyNotation) == 1 ? mc::XyNumbering::Octal : mc::XyNumbering::Hex;
    c.xyAsciiDigits = intAt(m_model, kXyAscii) == 1 ? mc::XyNumbering::Octal : mc::XyNumbering::Hex;
    return c;
}

MockSettings MockTab::mockSettings() const {
    MockSettings s;
    s.unsupportedQna = static_cast<quint16>(hexAt(m_model, kUnsupportedQna, s.unsupportedQna));
    s.unsupported1e = static_cast<quint8>(hexAt(m_model, kUnsupported1e, s.unsupported1e));
    s.unsupported1c = static_cast<quint8>(hexAt(m_model, kUnsupported1c, s.unsupported1c));
    s.sumErrorQna = static_cast<quint16>(hexAt(m_model, kSumErrorQna, s.sumErrorQna));
    s.sumError1c = static_cast<quint8>(hexAt(m_model, kSumError1c, s.sumError1c));
    s.outOfRangeQna = static_cast<quint16>(hexAt(m_model, kOutOfRangeQna, s.outOfRangeQna));
    s.outOfRange1e = static_cast<quint8>(hexAt(m_model, kOutOfRange1e, s.outOfRange1e));
    s.outOfRange1eAbnormal =
        static_cast<quint8>(hexAt(m_model, kOutOfRange1eAbn, s.outOfRange1eAbnormal));
    s.outOfRange1c = static_cast<quint8>(hexAt(m_model, kOutOfRange1c, s.outOfRange1c));
    return s;
}

SerialLine MockTab::serialLine() const {
    SerialLine line;
    line.portName = textAt(m_model, kComPort).trimmed();
    line.baudRate = intAt(m_model, kBaud);
    line.dataBits = intAt(m_model, kDataBits) == 1 ? 8 : 7;
    switch (intAt(m_model, kParity)) {
    case 0:
        line.parity = 0; // QSerialPort::NoParity
        break;
    case 2:
        line.parity = 3; // QSerialPort::OddParity
        break;
    default:
        line.parity = 2; // QSerialPort::EvenParity
        break;
    }
    line.stopBits = intAt(m_model, kStopBits) == 1 ? 2 : 1;
    return line;
}

bool MockTab::servesSerial() const {
    return intAt(m_model, kMode) == 1;
}

QString MockTab::configError() const {
    const auto valid = frameConfig().validate();
    if (!valid) {
        return QString::fromUtf8(valid.error().message);
    }
    if (servesSerial() && serialLine().portName.isEmpty()) {
        return QStringLiteral("the COM port name is empty");
    }
    return {};
}

QString MockTab::statusText() const {
    return m_message.isEmpty() ? m_status : m_status + QStringLiteral("  |  ") + m_message;
}

QString MockTab::logText() const {
    return m_logView->toPlainText();
}

void MockTab::buildUi() {
    m_start = new QPushButton(QStringLiteral("Start"));
    m_stop = new QPushButton(QStringLiteral("Stop"));
    m_statusLabel = new QLabel(statusText());
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_counters = new QLabel;
    onStats(MockStats{});

    auto* top = new QHBoxLayout;
    top->addWidget(m_start);
    top->addWidget(m_stop);
    top->addWidget(m_statusLabel, 1);

    auto* grid = new qpb::PropertyTreeView;
    grid->setModel(m_model);

    m_memory = new MockMemoryEditor;
    m_faults = new MockFaultPanel;

    m_requestLog = new MockRequestLogModel(this);
    m_requestView = new QTableView;
    m_requestView->setModel(m_requestLog);
    m_requestView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_requestView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_requestView->horizontalHeader()->setStretchLastSection(true);
    m_requestView->verticalHeader()->setVisible(false);
    auto* clearRequests = new QPushButton(QStringLiteral("Clear"));
    auto* requestPage = new QWidget;
    auto* requestLayout = new QVBoxLayout(requestPage);
    requestLayout->addWidget(clearRequests, 0, Qt::AlignLeft);
    requestLayout->addWidget(m_requestView, 1);

    m_logView = new QPlainTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(kMaxLogBlocks);
    m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto* clearLog = new QPushButton(QStringLiteral("Clear"));
    auto* logPage = new QWidget;
    auto* logLayout = new QVBoxLayout(logPage);
    logLayout->addWidget(clearLog, 0, Qt::AlignLeft);
    logLayout->addWidget(m_logView, 1);

    auto* keepPreset = new QPushButton(QStringLiteral("Keep this range as a preset"));
    keepPreset->setToolTip(QStringLiteral("The values shown are saved in the workspace and written "
                                          "back whenever the mock is built"));
    auto* clearPresets = new QPushButton(QStringLiteral("Clear presets"));
    m_presetLabel = new QLabel(QStringLiteral("Presets: 0"));
    auto* presetBar = new QHBoxLayout;
    presetBar->addWidget(keepPreset);
    presetBar->addWidget(clearPresets);
    presetBar->addWidget(m_presetLabel, 1);
    auto* memoryPage = new QWidget;
    auto* memoryLayout = new QVBoxLayout(memoryPage);
    memoryLayout->setContentsMargins(0, 0, 0, 0);
    memoryLayout->addWidget(m_memory, 1);
    memoryLayout->addLayout(presetBar);
    connect(keepPreset, &QPushButton::clicked, this, &MockTab::keepEditorRangeAsPreset);
    connect(clearPresets, &QPushButton::clicked, this, &MockTab::clearMemoryPresets);

    auto* tabs = new QTabWidget;
    tabs->addTab(memoryPage, QStringLiteral("Memory"));
    tabs->addTab(m_faults, QStringLiteral("Faults"));
    tabs->addTab(requestPage, QStringLiteral("Requests"));
    tabs->addTab(logPage, QStringLiteral("Log"));

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(grid);
    splitter->addWidget(tabs);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_counters);
    layout->addWidget(splitter, 1);

    connect(m_start, &QPushButton::clicked, this, &MockTab::startServing);
    connect(m_stop, &QPushButton::clicked, this, &MockTab::stopServing);
    connect(clearRequests, &QPushButton::clicked, m_requestLog, &MockRequestLogModel::clear);
    connect(clearLog, &QPushButton::clicked, m_logView, &QPlainTextEdit::clear);
}

void MockTab::connectHost() {
    connect(m_host, &MockHost::commandDone, this, [this](const CommandResult& r) { onCommandDone(r); });
    connect(m_host, &MockHost::servingChanged, this,
            [this](bool serving, const QString& what) { onServingChanged(serving, what); });
    connect(m_host, &MockHost::serialError, this, [this](const QString& message) {
        m_message = QStringLiteral("COM port closed: %1").arg(message);
        setStatus(m_status);
    });
    connect(m_host, &MockHost::statsChanged, this, [this](const MockStats& s) { onStats(s); });
    connect(m_host, &MockHost::requestsLogged, this, [this](const MockRequestBatch& batch) {
        QScrollBar* bar = m_requestView->verticalScrollBar();
        const bool follow = bar->value() >= bar->maximum();
        m_requestLog->append(batch);
        if (follow) {
            m_requestView->scrollToBottom();
        }
    });
    connect(m_host, &MockHost::logBatch, this, [this](const QVector<LogLine>& lines) { onLog(lines); });
    connect(m_host, &MockHost::memoryRead, this, [this](const MemoryBlock& block) {
        if (block.token != 0 && block.token == m_presetToken) {
            m_presetToken = 0;
            MemoryPreset preset;
            preset.head = block.head;
            preset.bits = block.bits;
            preset.values = block.values;
            addPreset(preset);
            return;
        }
        if (block.token == m_readToken) {
            m_readToken = 0;
        }
        m_memory->showBlock(block);
    });
    connect(m_host, &MockHost::failed, this, [this](const QString& message) {
        m_serving = false;
        m_starting = false;
        m_readToken = 0;
        m_message = QStringLiteral("runner stopped: %1").arg(message);
        setStatus(QStringLiteral("Stopped"));
        updateButtons();
        emit servingChanged(false);
    });

    connect(m_memory, &MockMemoryEditor::readRequested, this,
            [this](const QString& head, quint16 count, bool bits) { requestRead(head, count, bits); });
    connect(m_memory, &MockMemoryEditor::wordEdited, this,
            [this](const QString& device, quint16 value) {
                m_host->setWords(device, QVector<quint16>{value});
            });
    connect(m_memory, &MockMemoryEditor::bitEdited, this, [this](const QString& device, bool value) {
        m_host->setBits(device, QVector<bool>{value});
    });

    connect(m_faults, &MockFaultPanel::muteToggled, this, [this](bool on) { m_host->mute(on); });
    connect(m_faults, &MockFaultPanel::muteNextRequested, this,
            [this](quint32 count) { m_host->muteNext(count); });
    connect(m_faults, &MockFaultPanel::corruptRequested, this,
            [this](mc::Corruption mode, quint32 count) { m_host->corruptNext(mode, count); });
    connect(m_faults, &MockFaultPanel::failRangeRequested, this,
            [this](mc::DeviceType type, quint32 first, quint32 last, quint16 code, quint8 abnormal) {
                m_host->failRange(type, first, last, code, abnormal);
            });
    connect(m_faults, &MockFaultPanel::clearFaultsRequested, this, [this]() { m_host->clearFaults(); });
    connect(m_faults, &MockFaultPanel::deviceLimitRequested, this,
            [this](mc::DeviceType type, quint32 limit) { m_host->setDeviceLimit(type, limit); });

    m_model->onValueChanged(QString::fromLatin1(kLogLevel), this, [this](const QVariant& value) {
        static constexpr mc::LogLevel kLevels[] = {mc::LogLevel::Trace, mc::LogLevel::Debug,
                                                   mc::LogLevel::Info,  mc::LogLevel::Warn,
                                                   mc::LogLevel::Error, mc::LogLevel::Off};
        const int index = value.toInt();
        if (index >= 0 && index < 6) {
            m_host->setLogLevel(kLevels[index]);
        }
    });
}

namespace {

// Walks a settings object against the grid: every key must be a property (or a group, for an
// object); the problems carry the JSON path.
void checkSettingsKeys(const qpb::PropertyModel& model, const QJsonObject& object,
                       const QString& modelPath, const QString& jsonPath,
                       QVector<WorkspaceProblem>& out) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString path = modelPath.isEmpty() ? it.key() : modelPath + QLatin1Char('/') + it.key();
        const QString where = jsonPath + QLatin1Char('.') + it.key();
        const qpb::Property* property = model.find(path);
        if (property == nullptr) {
            out.push_back({where, QStringLiteral("unknown key")});
            continue;
        }
        const bool isGroup = dynamic_cast<const qpb::PropertyGroup*>(property) != nullptr;
        if (isGroup && !it.value().isObject()) {
            out.push_back({where, QStringLiteral("expected an object")});
        } else if (!isGroup && it.value().isObject()) {
            out.push_back({where, QStringLiteral("expected a value, not an object")});
        } else if (isGroup) {
            checkSettingsKeys(model, it.value().toObject(), path, where, out);
        }
    }
}

QString jsonText(const QJsonValue& value) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

// Every value the file holds must be the value the grid now holds: a number qpb clamped to its range
// (a port of 70000 becomes 65535) is reported with its path instead of being applied silently.
void reportChangedValues(const QJsonObject& wanted, const QJsonObject& now, const QString& jsonPath,
                         QVector<WorkspaceProblem>* out) {
    for (auto it = wanted.begin(); it != wanted.end(); ++it) {
        const QString where = jsonPath + QLatin1Char('.') + it.key();
        const QJsonValue got = now.value(it.key());
        if (it.value().isObject()) {
            reportChangedValues(it.value().toObject(), got.toObject(), where, out);
        } else {
            const bool same = (it.value().isDouble() && got.isDouble()) ? it.value().toDouble() == got.toDouble()
                                                                        : it.value() == got;
            if (!same) {
                out->push_back({where, QStringLiteral("%1 is outside what this setting accepts (it would become %2)")
                                           .arg(jsonText(it.value()), jsonText(got))});
            }
        }
    }
}

} // namespace

WorkspaceMock MockTab::saveState() const {
    WorkspaceMock state;
    state.name = m_name;
    state.settings = qpb::serialization::toJson(*m_model->root());
    state.presets = m_presets;
    return state;
}

bool MockTab::loadState(const WorkspaceMock& state, const QString& basePath,
                        QVector<WorkspaceProblem>* problems) {
    QVector<WorkspaceProblem> found;
    const QString settingsPath = basePath + QStringLiteral(".settings");
    checkSettingsKeys(*m_model, state.settings, QString(), settingsPath, found);
    if (!found.isEmpty()) {
        if (problems != nullptr) {
            *problems += found;
        }
        return false;
    }

    // Apply to the grid; the grid's own validators refuse what they refuse, with their message.
    QVector<WorkspaceProblem> refused;
    const QMetaObject::Connection watch =
        connect(m_model, &qpb::PropertyModel::validationFailed, this,
                [&](const QString& path, const QVariant&, const QString& message) {
                    QString dotted = path;
                    dotted.replace(QLatin1Char('/'), QLatin1Char('.'));
                    refused.push_back({settingsPath + QLatin1Char('.') + dotted, message});
                });
    const QJsonObject before = qpb::serialization::toJson(*m_model->root());
    const bool applied = qpb::serialization::fromJson(*m_model->root(), state.settings);
    disconnect(watch);
    if (applied && refused.isEmpty()) {
        // qpb clamps a number to the range of its property without a validation failure: a value
        // that did not come back as written is a refusal as well, with its path.
        reportChangedValues(state.settings, qpb::serialization::toJson(*m_model->root()), settingsPath,
                            &refused);
    }
    if (!applied || !refused.isEmpty()) {
        // Put the grid back: a refused file changes nothing.
        qpb::serialization::fromJson(*m_model->root(), before);
        if (refused.isEmpty()) {
            refused.push_back({settingsPath, QStringLiteral("a value is not accepted")});
        }
        if (problems != nullptr) {
            *problems += refused;
        }
        return false;
    }

    const mc::XyNumbering xy = frameConfig().xyNotation;
    const QString presetBase = basePath + QStringLiteral(".memory");
    for (int i = 0; i < state.presets.size(); ++i) {
        const MemoryPreset& preset = state.presets.at(i);
        const auto device = mc::parseDevice(preset.head.toStdString(), xy);
        const QString where = QStringLiteral("%1[%2].head").arg(presetBase).arg(i);
        if (!device) {
            found.push_back({where, QStringLiteral("not a device in this notation: %1").arg(preset.head)});
        } else if ((mc::deviceInfo(device.value().type).kind == mc::DeviceKind::Bit) != preset.bits) {
            found.push_back({where, QStringLiteral("%1 is a %2 device, the preset holds %3")
                                        .arg(preset.head,
                                             preset.bits ? QStringLiteral("word") : QStringLiteral("bit"),
                                             preset.bits ? QStringLiteral("bits") : QStringLiteral("words"))});
        } else {
            // The last point must be a device number the frame still accepts (the library's rule).
            mc::Device last = device.value();
            last.number += static_cast<uint32_t>(preset.values.size() > 0 ? preset.values.size() - 1 : 0);
            const mc::Request probe =
                preset.bits ? mc::Request::readBits(last, 1) : mc::Request::readWords(last, 1);
            const auto valid = mc::validate(probe, frameConfig());
            if (!valid) {
                found.push_back({where, QStringLiteral("%1 + %2 points runs past the last device this frame "
                                                       "accepts: %3")
                                            .arg(preset.head)
                                            .arg(preset.values.size())
                                            .arg(QString::fromUtf8(valid.error().message))});
            }
        }
    }
    if (!found.isEmpty()) {
        qpb::serialization::fromJson(*m_model->root(), before);
        if (problems != nullptr) {
            *problems += found;
        }
        return false;
    }

    m_presets = state.presets;
    m_memory->setXyNotation(xy);
    m_presetLabel->setText(QStringLiteral("Presets: %1").arg(m_presets.size()));
    emit presetsChanged();

    // The mock is rebuilt from the loaded frame and codes when it can be (not serving, frame valid);
    // the answer applies the presets. Otherwise the next start does both.
    if (!m_serving && !m_starting && m_loadToken == 0 && frameConfig().validate()) {
        m_loadToken = m_host->reconfigure(frameConfig(), mockSettings());
    }
    return true;
}

void MockTab::applyPresets() {
    for (const MemoryPreset& preset : m_presets) {
        if (preset.bits) {
            QVector<bool> bits;
            bits.reserve(preset.values.size());
            for (quint16 v : preset.values) {
                bits.push_back(v != 0);
            }
            m_host->setBits(preset.head, bits);
        } else {
            m_host->setWords(preset.head, preset.values);
        }
    }
}

void MockTab::setMemoryPresets(const QVector<MemoryPreset>& presets) {
    m_presets = presets;
    m_presetLabel->setText(QStringLiteral("Presets: %1").arg(m_presets.size()));
    if (!m_starting && !m_shapeDirty) {
        applyPresets();
    }
    emit presetsChanged();
}

void MockTab::addPreset(const MemoryPreset& preset) {
    for (MemoryPreset& existing : m_presets) {
        if (existing.head == preset.head && existing.bits == preset.bits) {
            existing = preset;
            emit presetsChanged();
            return;
        }
    }
    m_presets.push_back(preset);
    m_presetLabel->setText(QStringLiteral("Presets: %1").arg(m_presets.size()));
    emit presetsChanged();
}

void MockTab::keepEditorRangeAsPreset() {
    if (m_presetToken != 0) {
        return;
    }
    m_presetToken = m_host->readMemory(m_memory->head(), static_cast<quint16>(m_memory->count()),
                                       m_memory->isBitRange());
}

void MockTab::clearMemoryPresets() {
    m_presets.clear();
    m_presetLabel->setText(QStringLiteral("Presets: 0"));
    emit presetsChanged();
}

void MockTab::requestRead(const QString& head, quint16 count, bool bits) {
    if (m_readToken != 0) {
        return; // one read in flight is enough; the next refresh asks again
    }
    m_readToken = m_host->readMemory(head, count, bits);
}

void MockTab::setStatus(const QString& text) {
    m_status = text;
    m_statusLabel->setText(statusText());
    emit statusChanged(statusText());
}

void MockTab::updateButtons() {
    m_start->setEnabled(!m_serving && !m_starting);
    m_stop->setEnabled(m_serving);
}

void MockTab::startServing() {
    if (m_serving || m_starting) {
        return;
    }
    const QString error = configError();
    if (!error.isEmpty()) {
        m_message = QStringLiteral("settings are not valid: %1").arg(error);
        setStatus(QStringLiteral("Stopped"));
        return;
    }
    m_message.clear();
    m_starting = true;
    setStatus(QStringLiteral("Starting"));
    updateButtons();
    if (m_shapeDirty) {
        m_reconfigureToken = m_host->reconfigure(frameConfig(), mockSettings());
    } else {
        postServe();
    }
}

void MockTab::postServe() {
    if (servesSerial()) {
        m_serveToken = m_host->openSerial(serialLine());
    } else {
        m_serveToken = m_host->listen(static_cast<quint16>(intAt(m_model, kTcpPort)));
    }
}

void MockTab::stopServing() {
    m_host->stopListening();
}

void MockTab::onCommandDone(const CommandResult& r) {
    if (r.token != 0 && r.token == m_reconfigureToken) {
        m_reconfigureToken = 0;
        if (!r.ok) {
            m_starting = false;
            m_message = QStringLiteral("cannot apply the settings: %1").arg(r.message);
            setStatus(QStringLiteral("Stopped"));
            updateButtons();
            return;
        }
        m_shapeDirty = false;
        m_memory->setXyNotation(frameConfig().xyNotation);
        m_faults->setMuted(false);
        m_faults->clearFaultLines();
        applyPresets();
        postServe();
        return;
    }
    if (r.token != 0 && r.token == m_loadToken) {
        m_loadToken = 0;
        if (!r.ok) {
            m_message = QStringLiteral("cannot apply the loaded settings: %1").arg(r.message);
            setStatus(m_status);
            return;
        }
        m_shapeDirty = false;
        m_memory->setXyNotation(frameConfig().xyNotation);
        m_faults->setMuted(false);
        m_faults->clearFaultLines();
        applyPresets();
        emit stateLoaded();
        return;
    }
    if (r.token != 0 && r.token == m_presetToken) {
        if (!r.ok) {
            m_presetToken = 0;
            m_message = QStringLiteral("cannot keep the range as a preset: %1").arg(r.message);
            setStatus(m_status);
        }
        return;
    }
    if (r.token != 0 && r.token == m_serveToken) {
        m_serveToken = 0;
        m_starting = false;
        if (!r.ok) {
            m_message = QStringLiteral("cannot serve: %1").arg(r.message);
            setStatus(QStringLiteral("Stopped"));
        } else if (!servesSerial()) {
            m_port = static_cast<quint16>(r.value);
        }
        updateButtons();
        return;
    }
    if (r.token != 0 && r.token == m_readToken) {
        m_readToken = 0;
        if (!r.ok) {
            m_message = r.message;
            setStatus(m_status);
        }
        return;
    }
    if (!r.ok) {
        m_message = r.message;
        setStatus(m_status);
    }
}

void MockTab::onServingChanged(bool serving, const QString& what) {
    m_serving = serving;
    m_servingText = what;
    if (!serving) {
        m_port = 0;
    }
    setStatus(serving ? QStringLiteral("Serving ") + what : QStringLiteral("Stopped"));
    updateButtons();
    emit servingChanged(serving);
}

void MockTab::onStats(const MockStats& stats) {
    m_stats = stats;
    m_counters->setText(QStringLiteral("Requests: %1    EOT received: %2    Skipped bytes: %3    TCP clients: %4")
                            .arg(stats.requests)
                            .arg(stats.eotCount)
                            .arg(stats.skippedBytes)
                            .arg(stats.clients));
}

void MockTab::onLog(const QVector<LogLine>& lines) {
    QStringList text;
    text.reserve(static_cast<int>(lines.size()));
    for (const LogLine& line : lines) {
        text.append(QStringLiteral("%1 %2 %3 %4")
                        .arg(static_cast<double>(line.tNs) / 1e6, 10, 'f', 3)
                        .arg(QString::fromLatin1(levelName(line.level)), line.category, line.message));
    }
    if (!text.isEmpty()) {
        m_logView->appendPlainText(text.join(QLatin1Char('\n')));
    }
}

MockPane::MockPane(QWidget* parent)
    : QWidget(parent), m_tabs(new QTabWidget), m_empty(new QLabel) {
    m_tabs->setTabsClosable(true);
    m_tabs->setDocumentMode(true);
    m_empty->setText(QStringLiteral("No mock PLC yet. Press \"New mock\"."));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setEnabled(false);

    auto* add = new QPushButton(QStringLiteral("New mock"));
    auto* bar = new QHBoxLayout;
    bar->addWidget(add);
    bar->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(m_empty, 1);
    layout->addWidget(m_tabs, 1);
    m_tabs->setVisible(false);

    connect(add, &QPushButton::clicked, this, [this]() { addMock(); });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) { closeMock(index); });
}

MockTab* MockPane::addMock() {
    auto* tab = new MockTab(QStringLiteral("Mock %1").arg(++m_serial));
    adoptMock(tab);
    return tab;
}

void MockPane::adoptMock(MockTab* tab) {
    if (m_registry != nullptr) {
        m_registry->add(tab->telemetry());
    }
    const int index = m_tabs->addTab(tab, tab->name());
    m_tabs->setCurrentIndex(index);
    m_tabs->setVisible(true);
    m_empty->setVisible(false);
}

int MockPane::currentIndex() const {
    return m_tabs->currentIndex();
}

void MockPane::setCurrentIndex(int index) {
    if (index >= 0 && index < m_tabs->count()) {
        m_tabs->setCurrentIndex(index);
    }
}

void MockPane::setRegistry(TelemetryRegistry* registry) {
    m_registry = registry;
    if (m_registry == nullptr) {
        return;
    }
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (MockTab* tab = mockAt(i)) {
            m_registry->add(tab->telemetry());
        }
    }
}

int MockPane::mockCount() const {
    return m_tabs->count();
}

MockTab* MockPane::mockAt(int index) const {
    return qobject_cast<MockTab*>(m_tabs->widget(index));
}

void MockPane::closeMock(int index) {
    MockTab* tab = mockAt(index);
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
