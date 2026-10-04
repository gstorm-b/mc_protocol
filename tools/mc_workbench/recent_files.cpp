#include "mc_workbench/recent_files.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace mc::workbench {

namespace {
constexpr const char* kKey = "recentWorkspaces";
}

void RecentFiles::setStorePath(const QString& iniPath) {
    m_store = iniPath;
    if (m_store.isEmpty()) {
        return;
    }
    QSettings settings(m_store, QSettings::IniFormat);
    const QStringList saved = settings.value(QLatin1String(kKey)).toStringList();
    m_paths.clear();
    for (const QString& path : saved) {
        if (!path.isEmpty() && !m_paths.contains(path) && m_paths.size() < kMaxEntries) {
            m_paths.append(path);
        }
    }
}

void RecentFiles::add(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    m_paths.removeAll(absolute);
    m_paths.prepend(absolute);
    while (m_paths.size() > kMaxEntries) {
        m_paths.removeLast();
    }
    store();
}

void RecentFiles::remove(const QString& path) {
    if (m_paths.removeAll(path) > 0) {
        store();
    }
}

void RecentFiles::clear() {
    m_paths.clear();
    store();
}

void RecentFiles::store() const {
    if (m_store.isEmpty()) {
        return;
    }
    // The folder is created here, when the list is first saved, never at program start.
    QDir().mkpath(QFileInfo(m_store).absolutePath());
    QSettings settings(m_store, QSettings::IniFormat);
    settings.setValue(QLatin1String(kKey), m_paths);
    settings.sync();
}

} // namespace mc::workbench
