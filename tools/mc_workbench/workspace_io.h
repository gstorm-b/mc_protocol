/**
 * @file workspace_io.h
 * @brief `WorkspaceIo`: reads and writes workspace files on short-lived worker threads, so the GUI
 * thread never waits for the disk or for the JSON parser.
 */
#pragma once

#include "mc_workbench/workspace.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QThread;

namespace mc::workbench {

/**
 * @brief Saves and loads workspace files off the GUI thread.
 *
 * A save serialises on the worker and commits with `QSaveFile` (a temporary file in the same
 * folder, renamed over the target when complete), so a failed save never leaves a half-written
 * workspace. A load reads at most `kWorkspaceMaxBytes`, parses strictly and hands back either the
 * `Workspace` or the list of problems; it never touches a window.
 *
 * @note Lives on the GUI thread; the results arrive there as signals. Destroying it waits (bounded)
 *       for the workers.
 */
class WorkspaceIo : public QObject {
    Q_OBJECT
public:
    /// @brief Creates an idle object.
    /// @param[in] parent Qt parent.
    explicit WorkspaceIo(QObject* parent = nullptr);

    /// @brief Waits for the workers in progress.
    ~WorkspaceIo() override;

    /**
     * @brief Starts writing @p workspace to @p path.
     * @param[in] path The file.
     * @param[in] workspace What to write; copied.
     */
    void save(const QString& path, const Workspace& workspace);

    /**
     * @brief Starts reading and parsing @p path.
     * @param[in] path The file.
     */
    void load(const QString& path);

    /// @brief Whether a worker is running.
    /// @return true until the last result signal.
    bool busy() const noexcept { return !m_workers.isEmpty(); }

signals:
    /// @brief A save ended.
    /// @param[out] path The file.
    /// @param[out] ok Whether it was written.
    /// @param[out] message The error text; empty on success.
    void saved(const QString& path, bool ok, const QString& message);

    /// @brief A load ended.
    /// @param[out] path The file.
    /// @param[out] ok Whether the file is a good workspace.
    /// @param[out] workspace The content (empty unless @p ok).
    /// @param[out] problems Why the file is refused (empty when @p ok).
    void loaded(const QString& path, bool ok, const mc::workbench::Workspace& workspace,
                const QVector<mc::workbench::WorkspaceProblem>& problems);

private:
    void run(std::function<void()> job);

    QVector<QThread*> m_workers;
};

} // namespace mc::workbench
