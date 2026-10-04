/**
 * @file recent_files.h
 * @brief `RecentFiles`: the list of recently used workspace files.
 */
#pragma once

#include <QString>
#include <QStringList>

namespace mc::workbench {

/**
 * @brief A most-recent-first list of file paths, optionally kept in an INI file.
 *
 * Only the path of a workspace file is ever stored, never anything read from it (no address, no
 * COM name). Without a store path the list lives in memory only; the program gives it a file in the
 * user's configuration folder, tests give it one under the build tree or none.
 *
 * @note Not thread safe; GUI thread only.
 */
class RecentFiles {
public:
    /// @brief Most paths kept.
    static constexpr int kMaxEntries = 8;

    /// @brief Creates an empty, memory-only list.
    RecentFiles() = default;

    /**
     * @brief Keeps the list in an INI file and loads what it holds.
     * @param[in] iniPath The file; empty goes back to memory only (the list is kept as it is).
     */
    void setStorePath(const QString& iniPath);

    /// @brief The paths, most recent first.
    /// @return At most `kMaxEntries` absolute paths.
    QStringList paths() const { return m_paths; }

    /**
     * @brief Moves @p path to the front (adds it when new) and stores the list.
     * @param[in] path The workspace file.
     */
    void add(const QString& path);

    /**
     * @brief Drops @p path, e.g. when the file is gone, and stores the list.
     * @param[in] path The path as listed.
     */
    void remove(const QString& path);

    /// @brief Empties the list and the store.
    void clear();

private:
    void store() const;

    QStringList m_paths;
    QString m_store;
};

} // namespace mc::workbench
