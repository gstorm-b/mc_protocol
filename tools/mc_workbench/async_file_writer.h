/**
 * @file async_file_writer.h
 * @brief `AsyncFileWriter`: writes a text file on a short-lived thread, so the GUI thread never
 * waits for the disk (trace and log saves).
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class QThread;

namespace mc::workbench {

/**
 * @brief Saves text to a file on a worker thread and reports the result on the GUI thread.
 *
 * The text is handed over as a value (a copy that the caller bounded), so nothing is shared with the
 * GUI thread while the file is written. Destroying the writer waits for a write in progress.
 *
 * @note Lives on the GUI thread.
 */
class AsyncFileWriter : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Creates an idle writer.
     * @param[in] parent Qt parent.
     */
    explicit AsyncFileWriter(QObject* parent = nullptr);

    /// @brief Waits (bounded) for writes in progress.
    ~AsyncFileWriter() override;

    /**
     * @brief Starts writing @p text to @p path (UTF-8, replacing the file).
     * @param[in] path The file.
     * @param[in] text The content; copied.
     */
    void write(const QString& path, const QString& text);

    /// @brief Whether a write is in progress.
    /// @return true until the last `finished()`.
    bool busy() const noexcept { return !m_workers.isEmpty(); }

signals:
    /// @brief A write ended.
    /// @param[out] path The file.
    /// @param[out] ok Whether it was written.
    /// @param[out] message The error text; empty on success.
    void finished(const QString& path, bool ok, const QString& message);

private:
    QVector<QThread*> m_workers;
};

} // namespace mc::workbench
