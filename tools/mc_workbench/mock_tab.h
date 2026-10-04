/**
 * @file mock_tab.h
 * @brief `MockTab`: the view of one mock PLC (configuration grid, serving, memory editor, faults,
 * request log, debug log) and `MockPane`, the tab widget that holds several of them.
 */
#pragma once

#include "mc/core/frame_config.h"
#include "mc_workbench/mock_types.h"
#include "mc_workbench/runner_types.h"
#include "mc_workbench/workspace.h"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableView;

namespace qpb {
class PropertyModel;
} // namespace qpb

namespace mc::workbench {

class MockFaultPanel;
class MockHost;
class MockMemoryEditor;
class MockRequestLogModel;
class TabTelemetry;
class TelemetryRegistry;

/**
 * @brief One mock PLC as a tab (SPEC-gui-tool.md, "Mock PLC tab").
 *
 * The tab owns a `MockHost`: the `MockPlc`, its `QTcpServer` and its `QSerialPort` live on that
 * host's runner thread. The tab only renders, collects the settings (a qpb grid) and forwards
 * commands and value copies; it never touches the mock.
 *
 * Settings that shape the mock (the frame and the error codes) are applied by replacing the mock
 * when serving starts and they have changed, which empties its memory image and faults. Settings
 * of the serving endpoint (TCP port, COM port and line) and the log level do not touch the mock.
 *
 * @note GUI thread only. Destroying the tab stops the runner thread within
 *       `RunnerThread::kDefaultStopTimeoutMs`.
 */
class MockTab : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Builds the tab and starts its runner thread (not yet serving).
     * @param[in] name Name of the tab and of the runner thread.
     * @param[in] parent Qt parent.
     */
    explicit MockTab(const QString& name, QWidget* parent = nullptr);

    /// @brief Stops the runner thread (bounded) first, then deletes the widgets.
    ~MockTab() override;

    /// @brief The name given at construction.
    /// @return The tab name.
    QString name() const { return m_name; }

    /// @brief The GUI-side handle of the runner thread (for tests and the workspace).
    /// @return Never null.
    MockHost* host() const noexcept { return m_host; }

    /// @brief The trace ring, capture controls and log feed of this tab.
    /// @return Never null.
    TabTelemetry* telemetry() const noexcept { return m_telemetry; }

    /// @brief The qpb model of the settings grid.
    /// @return Never null.
    qpb::PropertyModel* configModel() const noexcept { return m_model; }

    /// @brief The memory editor.
    /// @return Never null.
    MockMemoryEditor* memoryEditor() const noexcept { return m_memory; }

    /// @brief The fault panel.
    /// @return Never null.
    MockFaultPanel* faultPanel() const noexcept { return m_faults; }

    /// @brief The request log.
    /// @return Never null.
    MockRequestLogModel* requestLog() const noexcept { return m_requestLog; }

    /// @brief The frame configuration the grid describes right now.
    /// @return A copy; may not pass `validate()` (see configError()).
    mc::FrameConfig frameConfig() const;

    /// @brief The error codes the grid describes right now.
    /// @return A copy.
    MockSettings mockSettings() const;

    /// @brief The COM line the grid describes right now.
    /// @return A copy.
    SerialLine serialLine() const;

    /// @brief Whether the grid says to serve a COM port (otherwise TCP).
    /// @return true for a COM port.
    bool servesSerial() const;

    /// @brief What is wrong with the settings, in the words of the library.
    /// @return Empty when the settings are valid.
    QString configError() const;

    /// @brief Whether the mock is serving (TCP or COM).
    /// @return true after the runner reported it.
    bool isServing() const noexcept { return m_serving; }

    /// @brief Whether a start is under way (waiting for the runner).
    /// @return true between startServing() and the answer.
    bool isStarting() const noexcept { return m_starting; }

    /// @brief The bound TCP port while serving TCP.
    /// @return The port; 0 otherwise.
    quint16 servingPort() const noexcept { return m_port; }

    /// @brief The last status line, e.g. "Serving tcp 127.0.0.1:5007".
    /// @return The text.
    QString statusText() const;

    /// @brief The last message of a refused command (empty when the last command succeeded).
    /// @return The text.
    QString lastMessage() const { return m_message; }

    /// @brief The last totals the runner reported.
    /// @return A copy.
    MockStats stats() const noexcept { return m_stats; }

    /// @brief The text of the debug log view.
    /// @return All lines currently kept.
    QString logText() const;

    /// @brief The tab as a workspace entry: its name, the settings grid and the memory presets.
    /// @return A copy; the serving state and the memory image are not part of it.
    WorkspaceMock saveState() const;

    /**
     * @brief Loads a workspace entry into this tab: the settings grid and the memory presets.
     *
     * Every key of the settings is checked against the grid first (an unknown key, a value of the
     * wrong kind or one the grid refuses is a problem); the tab is left as it was when anything is
     * wrong. A mock that is not serving is rebuilt at once from the loaded frame and error codes
     * (that empties its memory) and the presets are written into it; a mock that is serving keeps
     * running and takes the new settings at the next start.
     *
     * @param[in] state The entry (its name is not used here).
     * @param[in] basePath Path of the entry in the file, e.g. "mocks[0]", for the problem paths.
     * @param[out] problems Appended to; empty on success.
     * @return true when everything was loaded.
     */
    bool loadState(const WorkspaceMock& state, const QString& basePath,
                   QVector<WorkspaceProblem>* problems);

    /// @brief The memory presets written into the mock whenever it is built.
    /// @return A copy.
    QVector<MemoryPreset> memoryPresets() const { return m_presets; }

    /// @brief Replaces the presets and writes them into the mock now when it is not serving.
    /// @param[in] presets The new presets.
    void setMemoryPresets(const QVector<MemoryPreset>& presets);

    /// @brief Reads the range the memory editor shows and keeps it as a preset (the answer comes
    ///        asynchronously; a preset with the same head and kind is replaced).
    void keepEditorRangeAsPreset();

    /// @brief Forgets every preset (the memory image itself is not touched).
    void clearMemoryPresets();

public slots:
    /// @brief Applies changed settings and starts serving; the answer arrives asynchronously.
    void startServing();

    /// @brief Stops serving; the memory image stays.
    void stopServing();

signals:
    /// @brief The runner reported that serving started or stopped.
    /// @param[out] serving The new state.
    void servingChanged(bool serving);

    /// @brief The status line changed.
    /// @param[out] text The new status text.
    void statusChanged(const QString& text);

    /// @brief The memory presets changed (kept, cleared or loaded).
    void presetsChanged();

    /// @brief The mock was rebuilt after loadState() and its presets were sent.
    void stateLoaded();

private:
    void buildSettings();
    void buildUi();
    void connectHost();
    void setStatus(const QString& text);
    void onCommandDone(const CommandResult& result);
    void onServingChanged(bool serving, const QString& what);
    void onStats(const MockStats& stats);
    void onLog(const QVector<LogLine>& lines);
    void updateButtons();
    void postServe();
    void applyPresets();
    void addPreset(const MemoryPreset& preset);
    void requestRead(const QString& head, quint16 count, bool bits);

    QString m_name;
    MockHost* m_host{nullptr};
    TabTelemetry* m_telemetry{nullptr};
    qpb::PropertyModel* m_model{nullptr};
    MockMemoryEditor* m_memory{nullptr};
    MockFaultPanel* m_faults{nullptr};
    MockRequestLogModel* m_requestLog{nullptr};
    QTableView* m_requestView{nullptr};
    QPlainTextEdit* m_logView{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_counters{nullptr};
    QPushButton* m_start{nullptr};
    QPushButton* m_stop{nullptr};

    bool m_serving{false};
    bool m_starting{false};
    bool m_shapeDirty{false}; ///< The frame or the error codes changed since the mock was built.
    quint16 m_port{0};
    quint64 m_reconfigureToken{0};
    quint64 m_serveToken{0};
    quint64 m_readToken{0}; ///< The memory read in flight; 0 when none.
    quint64 m_presetToken{0}; ///< The read of "keep as preset" in flight; 0 when none.
    quint64 m_loadToken{0};   ///< The rebuild after loadState() in flight; 0 when none.
    QVector<MemoryPreset> m_presets;
    QLabel* m_presetLabel{nullptr};
    QString m_status{QStringLiteral("Stopped")};
    QString m_servingText;
    QString m_message;
    MockStats m_stats;
};

/**
 * @brief The "Mock PLCs" dock content: a button that adds a mock and a closable tab per mock.
 *
 * @note GUI thread only. Closing a tab stops its runner thread (bounded) and deletes it.
 */
class MockPane : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Builds the pane with no mock yet.
     * @param[in] parent Qt parent.
     */
    explicit MockPane(QWidget* parent = nullptr);

    /**
     * @brief Adds a mock tab named "Mock N" and makes it current.
     * @return The new tab; owned by the pane.
     */
    MockTab* addMock();

    /**
     * @brief Takes over a tab that was built elsewhere (the workspace loader builds its tabs first
     *        and adds them only when the whole file is good) and makes it a tab of the pane.
     * @param[in] tab The tab; owned by the pane from now on. Must not be in a pane already.
     */
    void adoptMock(MockTab* tab);

    /// @brief Index of the tab shown.
    /// @return The index; -1 when there is none.
    int currentIndex() const;

    /// @brief Shows the tab at an index.
    /// @param[in] index Tab index; ignored when out of range.
    void setCurrentIndex(int index);

    /// @brief Registers every mock tab (now and later) with the window's telemetry registry.
    /// @param[in] registry The registry; not owned, must outlive the pane.
    void setRegistry(TelemetryRegistry* registry);

    /// @brief Number of mock tabs.
    /// @return The count.
    int mockCount() const;

    /// @brief The mock tab at an index.
    /// @param[in] index Tab index.
    /// @return The tab, or null when out of range.
    MockTab* mockAt(int index) const;

    /// @brief Stops and deletes the mock tab at an index.
    /// @param[in] index Tab index; ignored when out of range.
    void closeMock(int index);

private:
    QTabWidget* m_tabs;
    QLabel* m_empty;
    TelemetryRegistry* m_registry{nullptr};
    int m_serial{0};
};

} // namespace mc::workbench
