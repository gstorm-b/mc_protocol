/**
 * @file capture_panel.h
 * @brief `CapturePanel`: record a tab's traffic, save it to a folder, export a real-PLC capture as
 * replay test data (SPEC-gui-tool.md, "Capture and export").
 */
#pragma once

#include "mc_workbench/capture_types.h"
#include "mc_workbench/tab_telemetry.h"

#include <QPointer>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace mc::workbench {

/**
 * @brief The capture controls of one tab.
 *
 * Recording happens on the tab's runner thread and the files are written by a worker thread of the
 * runner, so this panel only sends commands and shows the status. Where a capture may go is decided
 * by `checkOutputTarget()` on the runner: a capture of a mock PLC or `virtual_plc` is refused under
 * `tests/vectors/captured/`, whatever the panel does. The export button writes there only for the
 * source "Real PLC", after a confirmation that reminds the owner to review the files before
 * `git add`.
 *
 * @note GUI thread only.
 */
class CapturePanel : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Builds the panel; it is idle until setTelemetry().
     * @param[in] parent Parent widget.
     */
    explicit CapturePanel(QWidget* parent = nullptr);

    /**
     * @brief Controls the capture of @p telemetry's tab.
     * @param[in] telemetry The tab's telemetry; may be null. Not owned.
     */
    void setTelemetry(TabTelemetry* telemetry);

    /// @brief The protected folder `tests/vectors/captured/` of the repository.
    /// @param[in] path The folder; empty when unknown. The default is found from the program's
    ///        folder upwards.
    void setCapturedRoot(const QString& path);

    /// @brief The protected folder.
    /// @return The path; empty when the repository was not found.
    QString capturedRoot() const { return m_capturedRoot; }

    /// @brief The settings the widgets describe.
    /// @return Profile id, source, note and limit.
    CaptureSettings settings() const;

    /// @brief Chooses the source, as the combo box would.
    /// @param[in] source The source.
    void setSource(CaptureSource source);

    /// @brief Sets the profile id, as the line edit would.
    /// @param[in] profileId The folder-safe name.
    void setProfileId(const QString& profileId);

    /// @brief Sets the user folder captures are saved to.
    /// @param[in] folder The folder.
    void setFolder(const QString& folder);

    /// @brief Starts recording with settings().
    /// @return The token of the runner's answer; 0 when no tab is selected.
    quint64 start();

    /// @brief Stops recording.
    /// @return The token of the runner's answer.
    quint64 stop();

    /// @brief Writes the capture to `<folder>/<profile id>/`.
    /// @param[in] folder The folder to create the capture folder in.
    /// @param[in] overwrite Replace an existing capture folder.
    /// @return The token of the `captureSaved` that answers.
    quint64 saveTo(const QString& folder, bool overwrite = false);

    /// @brief Writes the capture to `<tests/vectors/captured>/<profile id>/` (no dialog).
    /// @param[in] overwrite Replace an existing capture folder.
    /// @return The token of the `captureSaved` that answers; 0 when the repository is unknown.
    quint64 exportAsReplayData(bool overwrite = false);

    /// @brief The status line.
    /// @return Plain text.
    QString statusText() const;

signals:
    /// @brief A save or export ended, or was refused.
    /// @param[out] result Token, success, message, files.
    void saved(const mc::workbench::CaptureSaveResult& result);

private:
    void updateWidgets();
    void onStatus(const CaptureStatus& status);
    void onSaved(const CaptureSaveResult& result);
    void onSaveClicked();
    void onExportClicked();

    QPointer<TabTelemetry> m_telemetry;
    QString m_capturedRoot;
    QComboBox* m_source;
    QLineEdit* m_profile;
    QLineEdit* m_note;
    QSpinBox* m_maxChunks;
    QPushButton* m_start;
    QPushButton* m_stop;
    QPushButton* m_discard;
    QLineEdit* m_folder;
    QPushButton* m_save;
    QPushButton* m_export;
    QLabel* m_status;
    QLabel* m_result;
};

} // namespace mc::workbench
