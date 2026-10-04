#include "mc_workbench/capture_panel.h"

#include "mc_workbench/capture_export.h"
#include "mc_workbench/tab_telemetry.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mc::workbench {

CapturePanel::CapturePanel(QWidget* parent)
    : QWidget(parent), m_capturedRoot(findCapturedRoot(QCoreApplication::applicationDirPath())),
      m_source(new QComboBox), m_profile(new QLineEdit(QStringLiteral("gui-capture"))),
      m_note(new QLineEdit), m_maxChunks(new QSpinBox),
      m_start(new QPushButton(QStringLiteral("Start"))),
      m_stop(new QPushButton(QStringLiteral("Stop"))),
      m_discard(new QPushButton(QStringLiteral("Discard"))),
      m_folder(new QLineEdit(QStringLiteral("captures"))),
      m_save(new QPushButton(QStringLiteral("Save to folder"))),
      m_export(new QPushButton(QStringLiteral("Export as replay data..."))), m_status(new QLabel),
      m_result(new QLabel) {
    m_source->addItem(QStringLiteral("Real PLC"), static_cast<int>(CaptureSource::RealPlc));
    m_source->addItem(QStringLiteral("Mock PLC"), static_cast<int>(CaptureSource::MockPlc));
    m_source->addItem(QStringLiteral("virtual_plc"), static_cast<int>(CaptureSource::VirtualPlc));
    m_source->setToolTip(QStringLiteral("What the traffic comes from. Only a real PLC may be "
                                        "exported to tests/vectors/captured/."));
    m_profile->setToolTip(QStringLiteral("Name of the capture folder (letters, digits, - _ .)"));
    m_note->setPlaceholderText(QStringLiteral("Operator note for run.meta (optional)"));
    m_maxChunks->setRange(1000, 5000000);
    m_maxChunks->setSingleStep(50000);
    m_maxChunks->setValue(static_cast<int>(CaptureSettings{}.maxChunks));
    m_maxChunks->setToolTip(QStringLiteral("Recording stops when this many chunks are held"));
    m_status->setWordWrap(true);
    m_result->setWordWrap(true);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* browse = new QPushButton(QStringLiteral("..."));
    browse->setFixedWidth(28);
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(browse);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_start);
    buttons->addWidget(m_stop);
    buttons->addWidget(m_discard);
    buttons->addStretch(1);

    auto* saveRow = new QHBoxLayout;
    saveRow->addWidget(m_save);
    saveRow->addWidget(m_export);
    saveRow->addStretch(1);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Source"), m_source);
    form->addRow(QStringLiteral("Profile id"), m_profile);
    form->addRow(QStringLiteral("Note"), m_note);
    form->addRow(QStringLiteral("Max chunks"), m_maxChunks);
    form->addRow(QStringLiteral("Folder"), folderRow);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addLayout(saveRow);
    layout->addWidget(m_status);
    layout->addWidget(m_result);

    connect(m_start, &QPushButton::clicked, this, [this]() { start(); });
    connect(m_stop, &QPushButton::clicked, this, [this]() { stop(); });
    connect(m_discard, &QPushButton::clicked, this, [this]() {
        if (m_telemetry != nullptr) {
            m_telemetry->discardCapture();
        }
    });
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString folder = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Folder for captures"), m_folder->text());
        if (!folder.isEmpty()) {
            m_folder->setText(folder);
        }
    });
    connect(m_save, &QPushButton::clicked, this, &CapturePanel::onSaveClicked);
    connect(m_export, &QPushButton::clicked, this, &CapturePanel::onExportClicked);
    connect(m_source, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateWidgets(); });
    updateWidgets();
}

void CapturePanel::setTelemetry(TabTelemetry* telemetry) {
    if (m_telemetry != nullptr) {
        disconnect(m_telemetry, nullptr, this, nullptr);
    }
    m_telemetry = telemetry;
    if (telemetry != nullptr) {
        connect(telemetry, &TabTelemetry::captureStatusChanged, this, &CapturePanel::onStatus);
        connect(telemetry, &TabTelemetry::captureSaved, this, &CapturePanel::onSaved);
        setSource(telemetry->suggestedSource());
    }
    m_result->clear();
    updateWidgets();
}

void CapturePanel::setCapturedRoot(const QString& path) {
    m_capturedRoot = path;
    updateWidgets();
}

CaptureSettings CapturePanel::settings() const {
    CaptureSettings s;
    s.profileId = m_profile->text().trimmed();
    s.source = static_cast<CaptureSource>(m_source->currentData().toInt());
    s.note = m_note->text();
    s.maxChunks = static_cast<quint32>(m_maxChunks->value());
    return s;
}

void CapturePanel::setSource(CaptureSource source) {
    const int index = m_source->findData(static_cast<int>(source));
    if (index >= 0) {
        m_source->setCurrentIndex(index);
    }
}

void CapturePanel::setProfileId(const QString& profileId) {
    m_profile->setText(profileId);
}

void CapturePanel::setFolder(const QString& folder) {
    m_folder->setText(folder);
}

quint64 CapturePanel::start() {
    if (m_telemetry == nullptr) {
        return 0;
    }
    m_result->clear();
    return m_telemetry->startCapture(settings());
}

quint64 CapturePanel::stop() {
    return m_telemetry != nullptr ? m_telemetry->stopCapture() : 0;
}

quint64 CapturePanel::saveTo(const QString& folder, bool overwrite) {
    if (m_telemetry == nullptr) {
        return 0;
    }
    CaptureSaveRequest request;
    request.outputRoot = folder;
    request.capturedRoot = m_capturedRoot;
    request.overwrite = overwrite;
    return m_telemetry->saveCapture(request);
}

quint64 CapturePanel::exportAsReplayData(bool overwrite) {
    if (m_capturedRoot.isEmpty()) {
        m_result->setText(QStringLiteral("The repository's tests/vectors/captured folder was not "
                                         "found from the program's folder."));
        return 0;
    }
    return saveTo(m_capturedRoot, overwrite);
}

QString CapturePanel::statusText() const {
    return m_status->text();
}

void CapturePanel::onStatus(const CaptureStatus& status) {
    QString text;
    if (status.active) {
        text = QStringLiteral("Recording: %1 chunks, %2 KiB").arg(status.chunks).arg(status.bytes / 1024);
    } else if (status.full) {
        text = QStringLiteral("Stopped, limit reached: %1 chunks, %2 KiB (can be saved)")
                   .arg(status.chunks)
                   .arg(status.bytes / 1024);
    } else if (status.hasData) {
        text = QStringLiteral("Stopped: %1 chunks, %2 KiB").arg(status.chunks).arg(status.bytes / 1024);
    } else {
        text = QStringLiteral("Nothing recorded");
    }
    m_status->setText(text);
    updateWidgets();
}

void CapturePanel::onSaved(const CaptureSaveResult& result) {
    m_result->setText(result.ok ? result.message : QStringLiteral("Not saved: %1").arg(result.message));
    emit saved(result);
}

void CapturePanel::onSaveClicked() {
    const QString folder = m_folder->text().trimmed();
    if (folder.isEmpty()) {
        m_result->setText(QStringLiteral("Choose a folder first."));
        return;
    }
    bool overwrite = false;
    const QString target = QDir(folder).absoluteFilePath(m_profile->text().trimmed());
    if (QFileInfo::exists(target)) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Replace the capture?"),
            QStringLiteral("%1 exists. Replace the files the tool wrote there?").arg(target));
        if (answer != QMessageBox::Yes) {
            return;
        }
        overwrite = true;
    }
    saveTo(folder, overwrite);
}

void CapturePanel::onExportClicked() {
    if (m_capturedRoot.isEmpty()) {
        exportAsReplayData();
        return;
    }
    const QString name = m_profile->text().trimmed();
    const QString target = QDir(m_capturedRoot).absoluteFilePath(name);
    QString text = QStringLiteral("This writes run.meta, steps.vec and session.vec to\n%1\n\n"
                                  "mc_replay_tests reads them as test data. Review the files before "
                                  "you run git add: they must come from a real PLC and hold no "
                                  "address or secret.")
                       .arg(target);
    bool overwrite = false;
    if (QFileInfo::exists(target)) {
        text += QStringLiteral("\n\nThe folder exists: its files will be REPLACED.");
        overwrite = true;
    }
    const auto answer = QMessageBox::question(this, QStringLiteral("Export as replay data"), text);
    if (answer == QMessageBox::Yes) {
        exportAsReplayData(overwrite);
    }
}

void CapturePanel::updateWidgets() {
    const bool tab = m_telemetry != nullptr;
    const CaptureStatus status = tab ? m_telemetry->captureStatus() : CaptureStatus{};
    m_start->setEnabled(tab && !status.active);
    m_stop->setEnabled(tab && status.active);
    m_discard->setEnabled(tab && !status.active && status.hasData);
    const bool canSave = tab && !status.active && status.hasData;
    m_save->setEnabled(canSave);
    const bool real = static_cast<CaptureSource>(m_source->currentData().toInt()) == CaptureSource::RealPlc;
    m_export->setEnabled(canSave && real && !m_capturedRoot.isEmpty());
    m_export->setToolTip(real ? (m_capturedRoot.isEmpty()
                                     ? QStringLiteral("tests/vectors/captured was not found")
                                     : QStringLiteral("Writes under %1").arg(m_capturedRoot))
                              : QStringLiteral("Only a capture of a real PLC may be exported to "
                                               "tests/vectors/captured/; save a mock capture to a "
                                               "folder instead"));
    if (!tab) {
        m_status->setText(QStringLiteral("No tab selected"));
    }
}

} // namespace mc::workbench
