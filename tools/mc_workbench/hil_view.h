/**
 * @file hil_view.h
 * @brief `HilView`: the "HIL runner" dock (SPEC-gui-tool.md, "HIL runner"). Load a profile and a
 * plan, run the `mc_hil_tool` safety gate, read the dry run, confirm, run with live step
 * outcomes, open the capture, replay it and read the bench report.
 */
#pragma once

#include "mc_workbench/capture_types.h"
#include "mc_workbench/hil_types.h"

#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;

namespace mc::workbench {

class HilHost;

/**
 * @brief The HIL runner view.
 *
 * Everything that touches a file, a PLC or a child process happens on the view's runner thread
 * (`HilHost`): the check (load, resolve, gate, dry run), the run, reading capture files, the
 * replay and the bench report. The view renders and forwards value copies.
 *
 * The safety gate is the gate of `hil_capture`. The "Run" button is enabled only after a check
 * that passed it, with the inputs unchanged since; it opens `HilConfirmDialog`, which needs the
 * typed profile id whenever the run holds read-only frames; the runner repeats the pipeline and
 * the confirmation rule before it starts anything. A refused plan shows the refusal text and
 * offers no way to run.
 *
 * @note GUI thread only.
 */
class HilView : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Builds the view and starts its runner thread.
     * @param[in] parent Parent widget.
     */
    explicit HilView(QWidget* parent = nullptr);

    /// @brief Cancels a run and stops the runner thread (bounded) before the widgets go.
    ~HilView() override;

    /// @brief The GUI-side handle of the runner thread (for tests).
    /// @return Never null.
    HilHost* host() const noexcept { return m_host; }

    /// @name Inputs, as the widgets would be edited
    /// @{
    /// @brief Sets the profile file.
    /// @param[in] path The file.
    void setProfilePath(const QString& path);
    /// @brief Sets the plan file.
    /// @param[in] path The file.
    void setPlanPath(const QString& path);
    /// @brief Sets the step groups to run (`--only`), e.g. "G1,G2"; empty runs every step.
    /// @param[in] groups Comma separated group ids.
    void setGroups(const QString& groups);
    /// @brief Sets the PLC state the operator set by hand.
    /// @param[in] state "RUN" or "STOP".
    void setPlcState(const QString& state);
    /// @brief Sets the folder that receives `<profile id>/`.
    /// @param[in] folder The folder.
    void setOutputRoot(const QString& folder);
    /// @brief Chooses what the run talks to; decides where the capture may go.
    /// @param[in] source The source.
    void setSource(CaptureSource source);
    /// @brief Sets the operator note recorded in `run.meta`.
    /// @param[in] note The text.
    void setNote(const QString& note);
    /// @brief Sets the repetitions of every bench step.
    /// @param[in] reps The number.
    void setBenchReps(int reps);
    /// @brief Allows replacing an existing capture folder of the same profile id.
    /// @param[in] on The new state.
    void setOverwrite(bool on);
    /// @brief Sets the protected folder `tests/vectors/captured/` (found upwards from the program
    ///        by default).
    /// @param[in] path The folder; empty when unknown.
    void setCapturedRoot(const QString& path);
    /// @brief Sets the `mc_replay_tests` executable (searched next to the program by default).
    /// @param[in] path The executable.
    void setReplayProgram(const QString& path);
    /// @}

    /// @brief The source the combo box shows.
    /// @return The source.
    CaptureSource source() const;
    /// @brief The folder that receives the capture folder.
    /// @return The path as typed.
    QString outputRoot() const;
    /// @brief The protected folder.
    /// @return The path; empty when unknown.
    QString capturedRoot() const { return m_capturedRoot; }
    /// @brief The `mc_replay_tests` executable.
    /// @return The path as shown.
    QString replayProgram() const;

    /// @brief The step groups as typed (`--only`).
    /// @return Comma separated group ids; empty runs every step.
    QString groups() const;
    /// @brief The PLC state the operator set.
    /// @return "RUN" or "STOP".
    QString plcState() const;
    /// @brief The operator note recorded in `run.meta`.
    /// @return The text.
    QString note() const;
    /// @brief The repetitions of every bench step.
    /// @return The number.
    int benchReps() const;
    /// @brief Whether an existing capture folder may be replaced.
    /// @return true when the box is ticked.
    bool overwrite() const;
    /// @brief The profile file as typed.
    /// @return The path.
    QString profilePath() const;
    /// @brief The plan file as typed.
    /// @return The path.
    QString planPath() const;

    /// @brief The inputs the widgets describe right now.
    /// @return Profile, plan, groups and PLC state.
    HilCheckInput input() const;

    /// @brief Runs the gate and the dry run on the runner thread.
    /// @return The token of the `checked` that answers.
    quint64 check();

    /// @brief Whether the Run button is enabled: a passed check, inputs unchanged, nothing running.
    /// @return true when a run may be requested.
    bool canRun() const;

    /// @brief What the Run button does: opens the confirmation dialog (modal) and, when it is
    ///        accepted, starts the run.
    /// @return The token of the `runFinished` that answers; 0 when not started.
    quint64 requestRun();

    /**
     * @brief Sends a run request with a confirmation already typed (what the dialog hands over).
     *
     * The runner repeats the check and the confirmation rule; a wrong id or a refused plan comes
     * back as a `runFinished` with a refusal status and sends nothing. Public so tests can prove
     * that; there is no argument that skips the rule.
     *
     * @param[in] typedId What the operator typed.
     * @param[in] skipTyping The operator asked to confirm without typing (`--yes`).
     * @return The token of the `runFinished` that answers; 0 when no check passed yet.
     */
    quint64 runWith(const QString& typedId, bool skipTyping = false);

    /// @brief Asks the run to stop after the current step.
    void cancelRun();

    /// @brief Answers the operator prompt of the plan ("press Enter to continue").
    void continueRun();

    /// @brief Whether a run is in progress.
    /// @return true between the start and `runFinished`.
    bool isRunning() const noexcept { return m_running; }

    /// @brief The result of the last check.
    /// @return A copy.
    HilCheckResult lastCheck() const { return m_check; }

    /// @brief The result of the last run request.
    /// @return A copy.
    HilRunResult lastRun() const { return m_run; }

    /// @brief The prompt the plan waits on right now.
    /// @return The text; empty when none.
    QString pendingPrompt() const { return m_prompt; }

    /// @brief The step that started last.
    /// @return The step id; empty before a run.
    QString currentStep() const { return m_current; }

    /// @brief Number of rows of the live outcome table.
    /// @return The count.
    int outcomeRows() const;

    /// @brief One cell of the live outcome table.
    /// @param[in] row The row.
    /// @param[in] column 0 step, 1 result, 2 text.
    /// @return The text.
    QString outcomeCell(int row, int column) const;

    /// @brief The status line.
    /// @return Plain text.
    QString statusText() const;

    /// @brief The text of the gate tab: the refusal, or the line that the gate passed.
    /// @return Plain text.
    QString gateText() const;

    /// @brief The text of the dry run tab.
    /// @return Plain text.
    QString dryRunText() const;

    /// @brief Reads a capture file of the last run on the runner thread and shows it.
    /// @param[in] name File name inside the capture folder, e.g. "steps.vec".
    /// @return The token of the `fileRead` that answers; 0 when there is no capture.
    quint64 showCaptureFile(const QString& name);

    /// @brief Runs `mc_replay_tests` on the output root on the runner thread.
    /// @return The token of the `replayDone` that answers.
    quint64 runReplay();

    /// @brief Builds the bench report of the output root on the runner thread and shows it.
    /// @return The token of the `benchReady` that answers.
    quint64 showBenchReport();

    /// @brief The text of the capture tab's file viewer.
    /// @return Plain text.
    QString fileViewText() const;

    /// @brief The replay line: green or failed.
    /// @return Plain text.
    QString replayStatusText() const;

    /// @brief The text of the replay output.
    /// @return Plain text.
    QString replayOutputText() const;

    /// @brief The text of the bench report.
    /// @return Plain text.
    QString benchText() const;

signals:
    /// @brief A check ended.
    /// @param[out] result The verdict and texts.
    void checkDone(const mc::workbench::HilCheckResult& result);
    /// @brief A run request ended or was refused.
    /// @param[out] result Status and counts.
    void runDone(const mc::workbench::HilRunResult& result);
    /// @brief The replay ended.
    /// @param[out] result Exit code and output.
    void replayFinished(const mc::workbench::HilReplayResult& result);
    /// @brief A capture file was read and shown.
    /// @param[out] file Path and text.
    void fileShown(const mc::workbench::HilFileText& file);
    /// @brief The bench report was built and shown.
    /// @param[out] result The text.
    void benchShown(const mc::workbench::HilBenchText& result);

private:
    void onChecked(const HilCheckResult& result);
    void onStepStarted(const QString& id);
    void onStepOutcome(const HilStepLine& line);
    void onOutputLine(const QString& line);
    void onPrompt(const QString& text);
    void onRunFinished(const HilRunResult& result);
    void updateButtons();
    void browseFile(QLineEdit* target, const QString& title, const QString& filter);
    void setStatus(const QString& text);
    QString findReplayProgram() const;

    HilHost* m_host;
    QString m_capturedRoot;

    QLineEdit* m_profile;
    QLineEdit* m_plan;
    QLineEdit* m_groups;
    QComboBox* m_state;
    QSpinBox* m_reps;
    QLineEdit* m_output;
    QComboBox* m_source;
    QLineEdit* m_note;
    QCheckBox* m_overwrite;
    QPushButton* m_checkButton;
    QPushButton* m_runButton;
    QPushButton* m_stopButton;
    QPushButton* m_continueButton;
    QLabel* m_status;
    QLabel* m_promptLabel;
    QLabel* m_stepLabel;
    QTabWidget* m_tabs;
    QPlainTextEdit* m_gate;
    QPlainTextEdit* m_dry;
    QTableWidget* m_outcomes;
    QPlainTextEdit* m_output_text;
    QLabel* m_folderLabel;
    QComboBox* m_fileCombo;
    QPlainTextEdit* m_fileView;
    QLineEdit* m_replayProgram;
    QLabel* m_replayStatus;
    QPlainTextEdit* m_replayOutput;
    QPlainTextEdit* m_bench;
    QPushButton* m_openFolder;
    QPushButton* m_replayButton;
    QPushButton* m_benchButton;

    HilCheckInput m_checkedInput;
    QString m_checkedOutput;
    HilCheckResult m_check;
    HilRunResult m_run;
    bool m_hasCheck{false};
    bool m_checking{false};
    bool m_running{false};
    QString m_prompt;
    QString m_current;
};

} // namespace mc::workbench
