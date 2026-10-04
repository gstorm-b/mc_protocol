/**
 * @file mock_memory_editor.h
 * @brief `MockMemoryEditor`: a device range of the memory image of a mock as an editable table.
 */
#pragma once

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc_workbench/mock_types.h"

#include <QString>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QTimer;

namespace mc::workbench {

/**
 * @brief Shows consecutive points of the memory image of a mock and sends the edits of the user
 * out.
 *
 * Pure view: it owns no mock. The tab connects `readRequested` to `MockHost::readMemory` and
 * feeds the answer back with showBlock(); an edit leaves as `wordEdited` or `bitEdited`. Words
 * show in decimal or hex, bits as 0 or 1. The cell being edited is never overwritten by a refresh.
 *
 * @note GUI thread only. Holds copies of the values, never a pointer into the runner.
 */
class MockMemoryEditor : public QWidget {
    Q_OBJECT
public:
    /// @brief Most points the table shows.
    static constexpr int kMaxPoints = 1024;

    /// @brief Milliseconds between two automatic refreshes while the editor is visible.
    static constexpr int kRefreshMs = 500;

    /**
     * @brief Builds the editor with head `D0` and 16 points.
     * @param[in] parent Qt parent.
     */
    explicit MockMemoryEditor(QWidget* parent = nullptr);

    /// @brief Sets the numeral base of X and Y device text (octal for FX CPUs).
    /// @param[in] notation The notation of the frame of the mock.
    void setXyNotation(mc::XyNumbering notation);

    /// @brief Sets the head device and the number of points.
    /// @param[in] head Device text, e.g. "D100" or "M0".
    /// @param[in] count Points, 1 to `kMaxPoints`.
    /// @return false when @p head does not parse or @p count is out of range (nothing changes).
    bool setRange(const QString& head, int count);

    /// @brief The head device as shown.
    /// @return The text.
    QString head() const;

    /// @brief The number of points as shown.
    /// @return The count.
    int count() const;

    /// @brief Whether the shown range is made of bit devices.
    /// @return true for X, Y, M and the other bit devices.
    bool isBitRange() const noexcept { return m_bits; }

    /// @brief Stops or resumes the automatic refresh (the tab pauses it while not serving).
    /// @param[in] on true to refresh every `kRefreshMs` while visible.
    void setAutoRefresh(bool on);

    /// @brief Emits `readRequested` for the shown range now.
    void refreshNow();

    /**
     * @brief Shows a block read from the mock; ignored when it is not for the shown range.
     * @param[in] block The copied points.
     */
    void showBlock(const MemoryBlock& block);

    /// @brief The value shown in a row.
    /// @param[in] row Row, 0 is the head device.
    /// @return The word or bit; 0 when out of range.
    quint16 valueAt(int row) const;

    /// @brief Types @p value into a row as the user would; emits `wordEdited` or `bitEdited`.
    /// @param[in] row Row, 0 is the head device.
    /// @param[in] value The word, or 0 / 1 for a bit.
    /// @return false when the row is out of range.
    bool editRow(int row, quint16 value);

signals:
    /// @brief The shown range wants a fresh copy.
    /// @param[out] head Head device text.
    /// @param[out] count Points.
    /// @param[out] bits true when the range is bits.
    void readRequested(const QString& head, quint16 count, bool bits);

    /// @brief The user edited a word.
    /// @param[out] device Device text of the row, e.g. "D101".
    /// @param[out] value The new value.
    void wordEdited(const QString& device, quint16 value);

    /// @brief The user edited a bit.
    /// @param[out] device Device text of the row, e.g. "M3".
    /// @param[out] value The new value.
    void bitEdited(const QString& device, bool value);

private:
    void rebuild();
    void onRangeEntered();
    void onItemChanged(QTableWidgetItem* item);
    QString deviceName(int row) const;
    QString valueText(quint16 value) const;
    bool parseValue(const QString& text, quint16& value) const;

    QLineEdit* m_headEdit;
    QSpinBox* m_countSpin;
    QCheckBox* m_hex;
    QLabel* m_hint;
    QTableWidget* m_table;
    QTimer* m_timer;
    mc::XyNumbering m_xy{mc::XyNumbering::Hex};
    mc::Device m_device{};
    bool m_bits{false};
    bool m_updating{false};
    QString m_head{QStringLiteral("D0")};
    int m_count{16};
};

} // namespace mc::workbench
