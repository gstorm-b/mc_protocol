/**
 * @file mock_fault_panel.h
 * @brief `MockFaultPanel`: the fault injection and corruption controls of a mock tab.
 */
#pragma once

#include "mc/core/device.h"
#include "mc/mock/mock_plc.h"

#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QSpinBox;

namespace mc::workbench {

/**
 * @brief Controls for `mute`, `muteNext`, `corruptNext`, `failRange`, `clearFaults` and
 * `setDeviceLimit` of a `MockPlc`.
 *
 * Pure view: every control only emits a request; the tab turns it into a `MockHost` call. The
 * list of faults shown is a record of what the user asked for in this panel (the library offers no
 * way to list its faults); it is emptied by clearFaultsRequested().
 *
 * @note GUI thread only.
 */
class MockFaultPanel : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Builds the panel.
     * @param[in] parent Qt parent.
     */
    explicit MockFaultPanel(QWidget* parent = nullptr);

    /// @brief Whether the "mute" box is ticked.
    /// @return true while the mock is asked to swallow every request.
    bool isMuted() const;

    /// @brief Ticks or clears the "mute" box without emitting `muteToggled`.
    /// @param[in] on The new state.
    void setMuted(bool on);

    /// @brief Empties the list of faults (the mock was replaced, so it has none).
    void clearFaultLines();

    /// @brief Texts of the faults the user added, oldest first.
    /// @return One line per fault.
    QStringList faultLines() const;

    /// @brief Presses "Add fault" with the given values, as the user would.
    /// @param[in] type Device type.
    /// @param[in] first First device number.
    /// @param[in] last Last device number.
    /// @param[in] code PLC end code.
    /// @param[in] abnormal 1E abnormal code.
    void requestFailRange(mc::DeviceType type, quint32 first, quint32 last, quint16 code,
                          quint8 abnormal);

    /// @brief Text of a corruption mode.
    /// @param[in] mode The mode.
    /// @return A short description, e.g. "WrongSumCheck".
    static QString corruptionName(mc::Corruption mode);

signals:
    /// @brief The "mute" box was toggled by the user.
    /// @param[out] on true to swallow every request.
    void muteToggled(bool on);

    /// @brief "Mute next" was pressed.
    /// @param[out] count Requests to swallow.
    void muteNextRequested(quint32 count);

    /// @brief "Corrupt next" was pressed.
    /// @param[out] mode How to corrupt.
    /// @param[out] count Responses to corrupt.
    void corruptRequested(mc::Corruption mode, quint32 count);

    /// @brief "Add fault" was pressed.
    /// @param[out] type Device type.
    /// @param[out] first First device number.
    /// @param[out] last Last device number.
    /// @param[out] code PLC end code.
    /// @param[out] abnormal 1E abnormal code.
    void failRangeRequested(mc::DeviceType type, quint32 first, quint32 last, quint16 code,
                            quint8 abnormal);

    /// @brief "Clear faults" was pressed.
    void clearFaultsRequested();

    /// @brief "Set limit" was pressed.
    /// @param[out] type Device type.
    /// @param[out] limit Number of points accepted.
    void deviceLimitRequested(mc::DeviceType type, quint32 limit);

private:
    void onAddFault();

    QCheckBox* m_mute;
    QSpinBox* m_muteCount;
    QComboBox* m_corruption;
    QSpinBox* m_corruptCount;
    QComboBox* m_faultType;
    QLineEdit* m_faultFirst;
    QLineEdit* m_faultLast;
    QLineEdit* m_faultCode;
    QLineEdit* m_faultAbnormal;
    QComboBox* m_limitType;
    QSpinBox* m_limit;
    QListWidget* m_faults;
    QLineEdit* m_hint;
};

} // namespace mc::workbench
