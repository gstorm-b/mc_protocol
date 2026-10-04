#include "mc_workbench/hil_view.h"

#include "mc_workbench/capture_export.h"
#include "mc_workbench/hil_confirm_dialog.h"
#include "mc_workbench/hil_host.h"

#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace mc::workbench {

namespace {

constexpr int kMaxOutputLines = 5000;

bool sameInput(const HilCheckInput& a, const HilCheckInput& b) {
    return a.profilePath == b.profilePath && a.planPath == b.planPath && a.only == b.only &&
           a.plcState.toUpper() == b.plcState.toUpper();
}

QPlainTextEdit* textView() {
    auto* view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    return view;
}

QString firstLine(const QString& text) {
    const int newline = text.indexOf(QLatin1Char('\n'));
    return newline < 0 ? text : text.left(newline) + QStringLiteral(" ...");
}

} // namespace

HilView::HilView(QWidget* parent) : QWidget(parent) {
    m_host = new HilHost(QStringLiteral("hil-runner"), this);

    auto* layout = new QVBoxLayout(this);

    auto* form = new QGridLayout;
    int row = 0;
    const auto addRow = [&](const QString& label, QWidget* field, QPushButton* extra = nullptr) {
        form->addWidget(new QLabel(label), row, 0);
        form->addWidget(field, row, 1);
        if (extra != nullptr) {
            form->addWidget(extra, row, 2);
        }
        ++row;
    };
    m_profile = new QLineEdit;
    m_profile->setPlaceholderText(QStringLiteral("profile JSON, e.g. tests/hil/profiles/....json"));
    auto* profileBrowse = new QPushButton(QStringLiteral("Browse..."));
    addRow(QStringLiteral("Profile"), m_profile, profileBrowse);
    m_plan = new QLineEdit;
    m_plan->setPlaceholderText(QStringLiteral("plan JSON, e.g. tests/hil/plans/....json"));
    auto* planBrowse = new QPushButton(QStringLiteral("Browse..."));
    addRow(QStringLiteral("Plan"), m_plan, planBrowse);
    m_groups = new QLineEdit;
    m_groups->setPlaceholderText(QStringLiteral("step groups, e.g. G1,G2,G8 (empty: every step)"));
    addRow(QStringLiteral("Groups"), m_groups);
    m_output = new QLineEdit(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("hil_captures")));
    auto* outputBrowse = new QPushButton(QStringLiteral("Browse..."));
    addRow(QStringLiteral("Capture folder"), m_output, outputBrowse);

    auto* options = new QHBoxLayout;
    m_state = new QComboBox;
    m_state->addItems({QStringLiteral("RUN"), QStringLiteral("STOP")});
    m_reps = new QSpinBox;
    m_reps->setRange(1, 100000);
    m_reps->setValue(200);
    m_source = new QComboBox;
    m_source->addItem(QStringLiteral("Real PLC"), static_cast<int>(CaptureSource::RealPlc));
    m_source->addItem(QStringLiteral("Mock PLC"), static_cast<int>(CaptureSource::MockPlc));
    m_source->addItem(QStringLiteral("virtual_plc"), static_cast<int>(CaptureSource::VirtualPlc));
    m_source->setCurrentIndex(1);
    m_overwrite = new QCheckBox(QStringLiteral("Replace an existing capture"));
    options->addWidget(new QLabel(QStringLiteral("PLC state")));
    options->addWidget(m_state);
    options->addWidget(new QLabel(QStringLiteral("Bench reps")));
    options->addWidget(m_reps);
    options->addWidget(new QLabel(QStringLiteral("Talks to")));
    options->addWidget(m_source);
    options->addWidget(m_overwrite);
    options->addStretch(1);
    form->addLayout(options, row++, 1, 1, 2);
    m_note = new QLineEdit;
    m_note->setPlaceholderText(QStringLiteral("operator note for run.meta"));
    addRow(QStringLiteral("Note"), m_note);
    form->setColumnStretch(1, 1);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout;
    m_checkButton = new QPushButton(QStringLiteral("Check (gate + dry run)"));
    m_runButton = new QPushButton(QStringLiteral("Run..."));
    m_stopButton = new QPushButton(QStringLiteral("Stop after this step"));
    m_continueButton = new QPushButton(QStringLiteral("Continue"));
    buttons->addWidget(m_checkButton);
    buttons->addWidget(m_runButton);
    buttons->addWidget(m_stopButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    m_status = new QLabel(QStringLiteral("Choose a profile and a plan, then Check."));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    m_promptLabel = new QLabel;
    m_promptLabel->setWordWrap(true);
    m_promptLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    auto* promptRow = new QHBoxLayout;
    promptRow->addWidget(m_promptLabel, 1);
    promptRow->addWidget(m_continueButton);
    layout->addLayout(promptRow);
    m_promptLabel->hide();
    m_continueButton->hide();

    m_tabs = new QTabWidget;
    layout->addWidget(m_tabs, 1);

    m_gate = textView();
    m_tabs->addTab(m_gate, QStringLiteral("Gate report"));
    m_dry = textView();
    m_tabs->addTab(m_dry, QStringLiteral("Dry run"));

    auto* live = new QWidget;
    auto* liveLayout = new QVBoxLayout(live);
    m_stepLabel = new QLabel(QStringLiteral("No run yet."));
    liveLayout->addWidget(m_stepLabel);
    m_outcomes = new QTableWidget(0, 3);
    m_outcomes->setHorizontalHeaderLabels({QStringLiteral("Step"), QStringLiteral("Result"), QStringLiteral("What")});
    m_outcomes->horizontalHeader()->setStretchLastSection(true);
    m_outcomes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_outcomes->setSelectionBehavior(QAbstractItemView::SelectRows);
    liveLayout->addWidget(m_outcomes, 2);
    m_output_text = textView();
    m_output_text->setMaximumBlockCount(kMaxOutputLines);
    liveLayout->addWidget(m_output_text, 1);
    m_tabs->addTab(live, QStringLiteral("Live run"));

    auto* capture = new QWidget;
    auto* captureLayout = new QVBoxLayout(capture);
    m_folderLabel = new QLabel(QStringLiteral("No capture yet."));
    m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    captureLayout->addWidget(m_folderLabel);
    auto* fileRow = new QHBoxLayout;
    m_fileCombo = new QComboBox;
    m_fileCombo->addItems({QStringLiteral("run.meta"), QStringLiteral("steps.vec"),
                           QStringLiteral("session.vec"), QStringLiteral("bench.csv"),
                           QStringLiteral("divergences.txt")});
    auto* showFile = new QPushButton(QStringLiteral("Show file"));
    m_openFolder = new QPushButton(QStringLiteral("Open folder"));
    fileRow->addWidget(m_fileCombo);
    fileRow->addWidget(showFile);
    fileRow->addWidget(m_openFolder);
    fileRow->addStretch(1);
    captureLayout->addLayout(fileRow);
    m_fileView = textView();
    captureLayout->addWidget(m_fileView, 2);
    auto* replayRow = new QHBoxLayout;
    m_replayProgram = new QLineEdit(findReplayProgram());
    m_replayProgram->setPlaceholderText(QStringLiteral("path of mc_replay_tests"));
    m_replayButton = new QPushButton(QStringLiteral("Run replay"));
    m_benchButton = new QPushButton(QStringLiteral("Bench report"));
    replayRow->addWidget(new QLabel(QStringLiteral("mc_replay_tests")));
    replayRow->addWidget(m_replayProgram, 1);
    replayRow->addWidget(m_replayButton);
    replayRow->addWidget(m_benchButton);
    captureLayout->addLayout(replayRow);
    m_replayStatus = new QLabel;
    captureLayout->addWidget(m_replayStatus);
    m_replayOutput = textView();
    captureLayout->addWidget(m_replayOutput, 1);
    m_bench = textView();
    captureLayout->addWidget(m_bench, 2);
    m_tabs->addTab(capture, QStringLiteral("Capture"));

    m_capturedRoot = findCapturedRoot(QCoreApplication::applicationDirPath());

    connect(profileBrowse, &QPushButton::clicked, this, [this]() {
        browseFile(m_profile, QStringLiteral("Profile"), QStringLiteral("Profiles (*.json)"));
    });
    connect(planBrowse, &QPushButton::clicked, this, [this]() {
        browseFile(m_plan, QStringLiteral("Plan"), QStringLiteral("Plans (*.json)"));
    });
    connect(outputBrowse, &QPushButton::clicked, this, [this]() {
        const QString dir =
            QFileDialog::getExistingDirectory(this, QStringLiteral("Capture folder"), m_output->text());
        if (!dir.isEmpty()) {
            m_output->setText(dir);
        }
    });
    connect(m_checkButton, &QPushButton::clicked, this, [this]() { check(); });
    connect(m_runButton, &QPushButton::clicked, this, [this]() { requestRun(); });
    connect(m_stopButton, &QPushButton::clicked, this, [this]() { cancelRun(); });
    connect(m_continueButton, &QPushButton::clicked, this, [this]() { continueRun(); });
    connect(showFile, &QPushButton::clicked, this,
            [this]() { showCaptureFile(m_fileCombo->currentText()); });
    connect(m_openFolder, &QPushButton::clicked, this, [this]() {
        if (!m_run.folder.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_run.folder));
        }
    });
    connect(m_replayButton, &QPushButton::clicked, this, [this]() { runReplay(); });
    connect(m_benchButton, &QPushButton::clicked, this, [this]() { showBenchReport(); });
    for (QLineEdit* edit : {m_profile, m_plan, m_groups}) {
        connect(edit, &QLineEdit::textChanged, this, [this]() { updateButtons(); });
    }
    connect(m_state, &QComboBox::currentTextChanged, this, [this]() { updateButtons(); });

    connect(m_host, &HilHost::checked, this, &HilView::onChecked);
    connect(m_host, &HilHost::stepStarted, this, &HilView::onStepStarted);
    connect(m_host, &HilHost::stepOutcome, this, &HilView::onStepOutcome);
    connect(m_host, &HilHost::outputLine, this, &HilView::onOutputLine);
    connect(m_host, &HilHost::promptRequested, this, &HilView::onPrompt);
    connect(m_host, &HilHost::runFinished, this, &HilView::onRunFinished);
    connect(m_host, &HilHost::fileRead, this, [this](const HilFileText& file) {
        m_fileView->setPlainText(file.ok && file.truncated
                                     ? file.text + QStringLiteral("\n... (file continues)")
                                     : file.text);
        emit fileShown(file);
    });
    connect(m_host, &HilHost::replayDone, this, [this](const HilReplayResult& result) {
        m_replayStatus->setText(!result.started
                                    ? QStringLiteral("Replay did not run.")
                                    : result.exitCode == 0
                                          ? QStringLiteral("Replay green (exit 0).")
                                          : QStringLiteral("Replay FAILED (exit %1).").arg(result.exitCode));
        m_replayOutput->setPlainText(result.output);
        updateButtons();
        emit replayFinished(result);
    });
    connect(m_host, &HilHost::benchReady, this, [this](const HilBenchText& result) {
        m_bench->setPlainText(result.text);
        emit benchShown(result);
    });
    connect(m_host, &HilHost::failed, this, [this](const QString& message) {
        setStatus(QStringLiteral("The HIL runner failed: %1").arg(message));
        m_running = false;
        m_checking = false;
        updateButtons();
    });
    updateButtons();
}

HilView::~HilView() {
    delete m_host; // cancels a run and joins the thread within the bound, before the widgets go
    m_host = nullptr;
}

void HilView::setProfilePath(const QString& path) { m_profile->setText(path); }
void HilView::setPlanPath(const QString& path) { m_plan->setText(path); }
void HilView::setGroups(const QString& groups) { m_groups->setText(groups); }
void HilView::setPlcState(const QString& state) { m_state->setCurrentText(state.toUpper()); }
void HilView::setOutputRoot(const QString& folder) { m_output->setText(folder); }
void HilView::setNote(const QString& note) { m_note->setText(note); }
void HilView::setBenchReps(int reps) { m_reps->setValue(reps); }
void HilView::setOverwrite(bool on) { m_overwrite->setChecked(on); }
void HilView::setCapturedRoot(const QString& path) { m_capturedRoot = path; }
void HilView::setReplayProgram(const QString& path) { m_replayProgram->setText(path); }

void HilView::setSource(CaptureSource source) {
    const int index = m_source->findData(static_cast<int>(source));
    if (index >= 0) {
        m_source->setCurrentIndex(index);
    }
}

CaptureSource HilView::source() const {
    return static_cast<CaptureSource>(m_source->currentData().toInt());
}

QString HilView::outputRoot() const { return m_output->text(); }
QString HilView::replayProgram() const { return m_replayProgram->text(); }

QString HilView::groups() const { return m_groups->text().trimmed(); }
QString HilView::plcState() const { return m_state->currentText(); }
QString HilView::note() const { return m_note->text(); }
int HilView::benchReps() const { return m_reps->value(); }
bool HilView::overwrite() const { return m_overwrite->isChecked(); }
QString HilView::profilePath() const { return m_profile->text().trimmed(); }
QString HilView::planPath() const { return m_plan->text().trimmed(); }

HilCheckInput HilView::input() const {
    HilCheckInput in;
    in.profilePath = m_profile->text().trimmed();
    in.planPath = m_plan->text().trimmed();
    for (const QString& group : m_groups->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        in.only.push_back(group.trimmed());
    }
    in.plcState = m_state->currentText();
    return in;
}

quint64 HilView::check() {
    m_checking = true;
    m_hasCheck = false;
    m_checkedInput = input();
    m_gate->setPlainText(QStringLiteral("Checking..."));
    m_dry->clear();
    setStatus(QStringLiteral("Checking the profile and the plan..."));
    updateButtons();
    return m_host->check(m_checkedInput);
}

void HilView::onChecked(const HilCheckResult& result) {
    m_checking = false;
    m_check = result;
    m_hasCheck = true;
    if (result.ok()) {
        QString text = QStringLiteral("The safety gate passed: %1 step(s) of plan %2, profile %3.\n")
                           .arg(result.stepCount)
                           .arg(result.planId, result.profileId);
        if (!result.readOnlyFrames.isEmpty()) {
            text += QStringLiteral("\n%1 read-only frame(s) the gate could not decode (the profile "
                                   "id must be typed to run):\n")
                        .arg(result.readOnlyFrames.size());
            for (const HilGateFrame& frame : result.readOnlyFrames) {
                text += QStringLiteral("  %1  %2\n    %3\n").arg(frame.stepId, frame.what, frame.hex);
            }
        }
        m_gate->setPlainText(text);
        m_dry->setPlainText(result.dryRunText);
        setSource(result.loopbackTcp ? CaptureSource::MockPlc : CaptureSource::RealPlc);
        setStatus(QStringLiteral("Gate passed. Read the dry run, then Run."));
    } else if (result.exitCode == 3) {
        m_gate->setPlainText(QStringLiteral("REFUSED by the safety gate. Nothing can be sent.\n\n") +
                             result.refusalText);
        m_dry->clear();
        setStatus(QStringLiteral("The safety gate refused the run; nothing is sent."));
        m_tabs->setCurrentWidget(m_gate);
    } else {
        m_gate->setPlainText(QStringLiteral("Cannot check:\n") + result.errorText);
        m_dry->clear();
        setStatus(QStringLiteral("Cannot check: %1").arg(firstLine(result.errorText)));
        m_tabs->setCurrentWidget(m_gate);
    }
    updateButtons();
    emit checkDone(result);
}

bool HilView::canRun() const {
    return m_hasCheck && m_check.ok() && !m_checking && !m_running &&
           sameInput(input(), m_checkedInput);
}

quint64 HilView::requestRun() {
    if (!canRun()) {
        return 0;
    }
    HilConfirmDialog dialog(m_check, this);
    if (dialog.exec() != QDialog::Accepted) {
        setStatus(QStringLiteral("Not confirmed; nothing was sent."));
        return 0;
    }
    return runWith(dialog.typedId(), dialog.skipTyping());
}

quint64 HilView::runWith(const QString& typedId, bool skipTyping) {
    // A refused plan has no run button and no run call: the view never even asks the runner.
    if (!m_hasCheck || !m_check.ok() || m_running || m_checking) {
        return 0;
    }
    HilRunRequest request;
    request.input = m_checkedInput;
    request.expectedDigest = m_check.digest;
    request.typedId = typedId;
    request.skipTyping = skipTyping;
    request.outputRoot = m_output->text().trimmed();
    request.capturedRoot = m_capturedRoot;
    request.source = source();
    request.note = m_note->text().trimmed();
    request.benchReps = m_reps->value();
    request.overwrite = m_overwrite->isChecked();

    m_running = true;
    m_prompt.clear();
    m_current.clear();
    m_outcomes->setRowCount(0);
    m_output_text->clear();
    m_stepLabel->setText(QStringLiteral("Starting..."));
    m_tabs->setCurrentIndex(2);
    setStatus(QStringLiteral("Running..."));
    updateButtons();
    return m_host->run(request);
}

void HilView::cancelRun() {
    if (m_running) {
        m_host->cancelRun();
        setStatus(QStringLiteral("Stopping after the current step..."));
    }
}

void HilView::continueRun() {
    if (!m_prompt.isEmpty()) {
        m_prompt.clear();
        m_promptLabel->hide();
        m_continueButton->hide();
        m_host->answerPrompt();
    }
}

void HilView::onStepStarted(const QString& id) {
    m_current = id;
    m_stepLabel->setText(QStringLiteral("Running step %1 (%2 done)").arg(id).arg(m_outcomes->rowCount()));
}

void HilView::onStepOutcome(const HilStepLine& line) {
    const int row = m_outcomes->rowCount();
    m_outcomes->insertRow(row);
    m_outcomes->setItem(row, 0, new QTableWidgetItem(line.stepId));
    auto* result = new QTableWidgetItem(line.category);
    QColor color;
    if (line.category == QLatin1String("PASS")) {
        color = QColor(0x2e, 0x7d, 0x32);
    } else if (line.category == QLatin1String("FAIL")) {
        color = QColor(0xc6, 0x28, 0x28);
    } else if (line.category == QLatin1String("SKIP")) {
        color = QColor(0x75, 0x75, 0x75);
    } else {
        color = QColor(0xb2, 0x6a, 0x00);
    }
    result->setForeground(QBrush(color));
    m_outcomes->setItem(row, 1, result);
    m_outcomes->setItem(row, 2, new QTableWidgetItem(line.text));
    m_outcomes->scrollToBottom();
}

void HilView::onOutputLine(const QString& line) {
    m_output_text->appendPlainText(line);
}

void HilView::onPrompt(const QString& text) {
    m_prompt = text.isEmpty() ? QStringLiteral("The plan waits for you.") : text;
    m_promptLabel->setText(m_prompt);
    m_promptLabel->show();
    m_continueButton->show();
}

void HilView::onRunFinished(const HilRunResult& result) {
    m_running = false;
    m_run = result;
    m_prompt.clear();
    m_promptLabel->hide();
    m_continueButton->hide();
    switch (result.status) {
    case HilRunStatus::Finished:
        setStatus(QStringLiteral("Finished: passed %1, failed %2, diverged %3, not supported %4, "
                                 "skipped %5. Capture: %6")
                      .arg(result.passed)
                      .arg(result.failed)
                      .arg(result.diverged)
                      .arg(result.notSupported)
                      .arg(result.skipped)
                      .arg(result.folder));
        m_folderLabel->setText(result.folder);
        m_stepLabel->setText(QStringLiteral("Run finished."));
        break;
    case HilRunStatus::Cancelled:
        setStatus(QStringLiteral("Stopped: %1").arg(result.reason));
        m_stepLabel->setText(QStringLiteral("Run stopped."));
        break;
    case HilRunStatus::GateRefused:
        setStatus(QStringLiteral("The safety gate refused the run; nothing was sent."));
        m_gate->setPlainText(result.reason);
        break;
    default:
        setStatus(QStringLiteral("Not run: %1").arg(firstLine(result.reason)));
        m_stepLabel->setText(QStringLiteral("Not run."));
        break;
    }
    updateButtons();
    emit runDone(result);
}

void HilView::updateButtons() {
    m_checkButton->setEnabled(!m_running && !m_checking);
    m_runButton->setEnabled(canRun());
    m_stopButton->setEnabled(m_running);
    const bool hasCapture = m_run.status == HilRunStatus::Finished && !m_run.folder.isEmpty();
    m_openFolder->setEnabled(hasCapture);
    m_fileCombo->setEnabled(hasCapture);
    m_replayButton->setEnabled(!m_running);
    m_benchButton->setEnabled(!m_running);
}

void HilView::browseFile(QLineEdit* target, const QString& title, const QString& filter) {
    const QString file = QFileDialog::getOpenFileName(this, title, target->text(), filter);
    if (!file.isEmpty()) {
        target->setText(file);
    }
}

void HilView::setStatus(const QString& text) {
    m_status->setText(text);
}

QString HilView::findReplayProgram() const {
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("mc_replay_tests.exe");
#else
    const QString name = QStringLiteral("mc_replay_tests");
#endif
    // Next to the program, then the build trees of CMake (tests/) and qmake (tests/qmake/<cfg>/).
    const QStringList candidates{
        QDir(dir).filePath(name),
        QDir(dir).filePath(QStringLiteral("../../tests/") + name),
        QDir(dir).filePath(QStringLiteral("../../tests/qmake/debug/") + name),
        QDir(dir).filePath(QStringLiteral("../../tests/qmake/release/") + name),
        QDir(dir).filePath(QStringLiteral("../../../tests/") + name),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return QString();
}

int HilView::outcomeRows() const { return m_outcomes->rowCount(); }

QString HilView::outcomeCell(int row, int column) const {
    const QTableWidgetItem* item = m_outcomes->item(row, column);
    return item == nullptr ? QString() : item->text();
}

QString HilView::statusText() const { return m_status->text(); }
QString HilView::gateText() const { return m_gate->toPlainText(); }
QString HilView::dryRunText() const { return m_dry->toPlainText(); }
QString HilView::fileViewText() const { return m_fileView->toPlainText(); }
QString HilView::replayStatusText() const { return m_replayStatus->text(); }
QString HilView::replayOutputText() const { return m_replayOutput->toPlainText(); }
QString HilView::benchText() const { return m_bench->toPlainText(); }

quint64 HilView::showCaptureFile(const QString& name) {
    if (m_run.folder.isEmpty()) {
        return 0;
    }
    return m_host->readFile(QDir(m_run.folder).filePath(name));
}

quint64 HilView::runReplay() {
    m_replayStatus->setText(QStringLiteral("Replaying..."));
    m_replayOutput->clear();
    return m_host->runReplay(m_replayProgram->text().trimmed(), m_output->text().trimmed());
}

quint64 HilView::showBenchReport() {
    return m_host->benchReport(m_output->text().trimmed());
}

} // namespace mc::workbench
