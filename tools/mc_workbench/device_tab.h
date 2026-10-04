/**
 * @file device_tab.h
 * @brief `DeviceTab`: one PLC connection of MC Workbench, with its config grid, link state,
 * subscriptions table, trend and ad-hoc console. The device itself lives on a runner thread.
 */
#pragma once

#include "mc/core/log.h"
#include "mc/device/mc_device.h"
#include "mc/device/mc_device_config.h"
#include "mc_workbench/console_model.h"
#include "mc_workbench/runner_types.h"

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableView;
class QTabWidget;

namespace qpb {
class PropertyTreeView;
} // namespace qpb

namespace mc::workbench {

class ConfigBinding;
class DeviceHost;
class PointTableModel;
class TabTelemetry;
class TelemetryRegistry;
class TrendWidget;

/**
 * @brief The "Device tab" row of the spec's Functions table.
 *
 * Shows the `McDeviceConfig` in a qpb grid (every edit checked with the library's `validate()`;
 * a refused edit keeps the old value and shows the library's message), connect / disconnect, the
 * link state and the last fault, the subscribed points with live values (changed values
 * highlighted), a trend of ticked points and a console for ad-hoc reads and writes with their
 * token and request id.
 *
 * All device work goes through a `DeviceHost`: this widget lives on the GUI thread, holds only
 * value copies and never touches a `McDevice`. An accepted configuration edit is sent to the
 * runner at once (a new `McDevice` is built there); while the link is not Disconnected the grid is
 * read-only.
 *
 * @note GUI thread only. Destroying the tab stops its runner thread (bounded, see `DeviceHost`).
 */
class DeviceTab : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Creates the tab and starts its runner thread.
     * @param[in] name Name of the tab and of its thread.
     * @param[in] parent Parent widget.
     */
    explicit DeviceTab(const QString& name, QWidget* parent = nullptr);

    /// @brief Stops the runner thread (bounded) and deletes the tab.
    ~DeviceTab() override;

    /// @brief The tab's name.
    /// @return The name given at construction.
    QString name() const { return m_name; }

    /// @brief The GUI-side handle of the runner thread (for tests and the shell of the window).
    /// @return Never null.
    DeviceHost* host() const noexcept { return m_host; }

    /// @brief The grid's binding.
    /// @return Never null.
    ConfigBinding* binding() const noexcept { return m_binding; }

    /// @brief The subscribed points.
    /// @return Never null.
    PointTableModel* points() const noexcept { return m_points; }

    /// @brief The trend chart.
    /// @return Never null.
    TrendWidget* trend() const noexcept { return m_trend; }

    /// @brief The console log.
    /// @return Never null.
    ConsoleModel* console() const noexcept { return m_console; }

    /// @brief The trace ring, capture controls and log feed of this tab.
    /// @return Never null.
    TabTelemetry* telemetry() const noexcept { return m_telemetry; }

    /// @brief The link state last reported by the runner.
    /// @return `Disconnected` until the first report.
    mc::LinkState linkState() const noexcept { return m_state; }

    /// @brief The line under the grid: the last refusal, failure or command error; empty when none.
    /// @return Plain text.
    QString message() const;

    /// @brief The link state line (state, reason and detail) next to the buttons.
    /// @return Plain text.
    QString linkStatusText() const;

    /// @brief The fault line next to the link state; empty when none.
    /// @return Plain text.
    QString faultText() const;

    /// @brief Subscriptions added at run time and still known (not the configured ones).
    /// @return Their number.
    int runtimeSubscriptionCount() const;

    /**
     * @brief Loads a whole configuration into the grid and sends it to the runner.
     * @param[in] cfg The configuration.
     * @param[out] error Optional. The library's message when @p cfg is refused, or why the tab
     *             could not apply it (the link is not Disconnected).
     * @return true when it was loaded and sent; `configApplied()` reports the runner's answer.
     */
    bool setConfig(const mc::McDeviceConfig& cfg, QString* error = nullptr);

    /// @brief Opens the link; progress arrives as `linkStateChanged()`.
    void connectToPlc();

    /// @brief Closes the link.
    void disconnectFromPlc();

    /**
     * @brief Subscribes to a range at run time (dropped when the configuration is applied again).
     * @param[in] device Head device, e.g. "D100".
     * @param[in] count Number of points.
     * @return The token of the runner's answer.
     */
    quint64 subscribe(const QString& device, quint32 count);

    /**
     * @brief Sends an ad-hoc read.
     * @param[in] bits true to read bit devices, false for words.
     * @param[in] head Head device, e.g. "D100".
     * @param[in] count Number of points, 1 to 65535.
     * @param[out] error Optional. Why nothing was sent.
     * @return The command's token, or 0 when the input was refused locally.
     */
    quint64 sendRead(bool bits, const QString& head, quint32 count, QString* error = nullptr);

    /**
     * @brief Sends an ad-hoc write.
     * @param[in] bits true to write bit devices, false for words.
     * @param[in] head Head device, e.g. "D100".
     * @param[in] valuesText Values such as "1 2 0x10" (words) or "1 0 1" (bits).
     * @param[out] error Optional. Why nothing was sent.
     * @return The command's token, or 0 when the input was refused locally.
     */
    quint64 sendWrite(bool bits, const QString& head, const QString& valuesText,
                      QString* error = nullptr);

signals:
    /// @brief The link state changed.
    /// @param[out] state The new state.
    void linkStateChanged(mc::LinkState state);

    /// @brief The runner answered a configuration.
    /// @param[out] ok Whether a new device was built.
    /// @param[out] message The reason when not.
    void configApplied(bool ok, const QString& message);

private:
    void buildUi();
    void sendConfig(const mc::McDeviceConfig& cfg);
    void onLinkState(mc::LinkState state, mc::LinkReason reason, const QString& detail);
    void onFault(const FaultReport& fault);
    void onCommandDone(const CommandResult& result);
    void showMessage(const QString& text, bool isError);
    void updateButtons();
    void onSendClicked();
    void onAddSubscription();
    void onRemoveSubscription();

    QString m_name;
    DeviceHost* m_host{nullptr};
    ConfigBinding* m_binding{nullptr};
    PointTableModel* m_points{nullptr};
    ConsoleModel* m_console{nullptr};
    TabTelemetry* m_telemetry{nullptr};
    TrendWidget* m_trend{nullptr};
    mc::LinkState m_state{mc::LinkState::Disconnected};
    QElapsedTimer m_clock;

    struct PendingSubscription {
        QString device;
        quint32 count{0};
    };
    QSet<quint64> m_configTokens; ///< applyConfig commands the runner has not answered yet
    QHash<quint64, PendingSubscription> m_pendingSubscribe;
    QHash<quint64, quint32> m_pendingUnsubscribe;

    QPushButton* m_connectButton{nullptr};
    QPushButton* m_disconnectButton{nullptr};
    QLabel* m_linkLabel{nullptr};
    QLabel* m_faultLabel{nullptr};
    QLabel* m_messageLabel{nullptr};
    qpb::PropertyTreeView* m_grid{nullptr};
    QLineEdit* m_subDevice{nullptr};
    QSpinBox* m_subCount{nullptr};
    QListWidget* m_subList{nullptr};
    QPushButton* m_subAdd{nullptr};
    QPushButton* m_subRemove{nullptr};
    QTableView* m_pointView{nullptr};
    QComboBox* m_consoleOp{nullptr};
    QLineEdit* m_consoleHead{nullptr};
    QSpinBox* m_consoleCount{nullptr};
    QLineEdit* m_consoleValues{nullptr};
    QPushButton* m_consoleSend{nullptr};
    QLabel* m_consoleError{nullptr};
    QTableView* m_consoleView{nullptr};
};

/**
 * @brief The content of the "Devices" dock: a "New device" button and one closable tab per PLC.
 *
 * @note GUI thread only. Closing a tab stops its runner thread (bounded) and deletes it.
 */
class DevicePane : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Builds the pane with no device yet.
     * @param[in] parent Qt parent.
     */
    explicit DevicePane(QWidget* parent = nullptr);

    /**
     * @brief Adds a device tab named "PLC N" and makes it current.
     * @return The new tab; owned by the pane.
     */
    DeviceTab* addDevice();

    /**
     * @brief Takes over a tab that was built elsewhere (the workspace loader builds its tabs first
     *        and adds them only when the whole file is good) and makes it a tab of the pane.
     * @param[in] tab The tab; owned by the pane from now on. Must not be in a pane already.
     */
    void adoptDevice(DeviceTab* tab);

    /// @brief Index of the tab shown.
    /// @return The index; -1 when there is none.
    int currentIndex() const;

    /// @brief Shows the tab at an index.
    /// @param[in] index Tab index; ignored when out of range.
    void setCurrentIndex(int index);

    /// @brief Registers every device tab (now and later) with the window's telemetry registry.
    /// @param[in] registry The registry; not owned, must outlive the pane.
    void setRegistry(TelemetryRegistry* registry);

    /// @brief Number of device tabs.
    /// @return The count.
    int deviceCount() const;

    /// @brief The device tab at an index.
    /// @param[in] index Tab index.
    /// @return The tab, or null when out of range.
    DeviceTab* deviceAt(int index) const;

    /// @brief Stops and deletes the device tab at an index.
    /// @param[in] index Tab index; ignored when out of range.
    void closeDevice(int index);

private:
    QTabWidget* m_tabs;
    QLabel* m_empty;
    TelemetryRegistry* m_registry{nullptr};
    int m_serial{0};
};

} // namespace mc::workbench
