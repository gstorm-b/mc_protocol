/**
 * @file workspace_controller.h
 * @brief `WorkspaceController`: turns the main window into a `Workspace` and back, and runs the
 * open, save, save as and recent-files commands (SPEC-gui-tool.md, "Workspace").
 */
#pragma once

#include "mc_workbench/recent_files.h"
#include "mc_workbench/workspace.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mc::workbench {

class MainWindow;
class WorkspaceIo;

/**
 * @brief Captures and applies a workspace; file work goes through `WorkspaceIo` on worker threads.
 *
 * Capturing copies what the tabs hold (configurations, mock settings and presets, the HIL view
 * inputs, the dock layout) on the GUI thread; nothing is read from a runner thread. Applying is
 * all-or-nothing for the tabs: every device and mock tab of the file is built first (each starts
 * its own runner thread and runs every check of the library on its configuration), and only when
 * all of them are good are the old tabs closed and the new ones shown. A refused file leaves the
 * window exactly as it was.
 *
 * Nothing about a device is written anywhere but the workspace file the user saves: the recent
 * list holds file paths only.
 *
 * @note GUI thread only.
 */
class WorkspaceController : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Creates the controller of @p window.
     * @param[in] window The window; it must outlive this object.
     */
    explicit WorkspaceController(MainWindow* window);

    /// @brief Waits for file work in progress.
    ~WorkspaceController() override;

    /// @brief Copies the window into a workspace value.
    /// @return Device tabs, mock tabs, the HIL view inputs and the layout.
    Workspace capture() const;

    /**
     * @brief Applies a workspace to the window.
     * @param[in] workspace What to show; replaces every device and mock tab.
     * @param[out] problems Optional. Appended to when a tab refuses its entry (the window is then
     *             unchanged).
     * @param[out] warnings Optional. Appended to when something non-essential could not be done
     *             (the dock layout did not restore).
     * @return true when the tabs were replaced.
     */
    bool apply(const Workspace& workspace, QVector<WorkspaceProblem>* problems = nullptr,
               QStringList* warnings = nullptr);

    /**
     * @brief Captures the window now and writes it to @p path on a worker thread.
     * @param[in] path The file.
     * @return false when another file operation is still running (nothing is started).
     */
    bool save(const QString& path);

    /**
     * @brief Reads @p path on a worker thread, then applies it.
     * @param[in] path The file.
     * @return false when another file operation is still running (nothing is started).
     */
    bool load(const QString& path);

    /// @brief The file of the current workspace (last saved or loaded).
    /// @return The path; empty before the first save or load.
    QString path() const { return m_path; }

    /// @brief Whether a file operation is running.
    /// @return true between save() / load() and its finished signal.
    bool busy() const;

    /// @brief The recent files.
    /// @return The list; edit it to set its store.
    RecentFiles& recent() { return m_recent; }

    /// @brief The recent files.
    /// @return The list.
    const RecentFiles& recent() const { return m_recent; }

signals:
    /// @brief A save ended.
    /// @param[out] path The file.
    /// @param[out] ok Whether it was written.
    /// @param[out] message One line for the status bar, or the error text.
    void saveFinished(const QString& path, bool ok, const QString& message);

    /// @brief A load ended (the file was read and, when good, applied).
    /// @param[out] path The file.
    /// @param[out] ok Whether the workspace was applied.
    /// @param[out] message One line for the status bar, or the refusal with every problem.
    void loadFinished(const QString& path, bool ok, const QString& message);

private:
    void onLoaded(const QString& path, bool ok, const Workspace& workspace,
                  const QVector<WorkspaceProblem>& problems);
    void onSaved(const QString& path, bool ok, const QString& message);
    void setPath(const QString& path);

    MainWindow* m_window;
    WorkspaceIo* m_io;
    RecentFiles m_recent;
    QString m_path;
    bool m_loading{false};
    bool m_saving{false};
};

} // namespace mc::workbench
