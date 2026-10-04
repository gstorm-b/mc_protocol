/**
 * @file main_window.h
 * @brief The main window of MC Workbench: a docking area with placeholder views, a menu and a
 *        status bar.
 */
#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class QMenu;

namespace ads {
class CDockManager;
class CDockWidget;
} // namespace ads

namespace qpb {
class PropertyModel;
} // namespace qpb

namespace mc::workbench {

class DevicePane;
class HilView;
class LogView;
class MockPane;
class TelemetryRegistry;
class TracePane;
class WorkspaceController;

/**
 * @brief Text of the About box: the versions of the library, Qt, the docking system and qpb.
 * @return Multi-line plain text.
 */
QString aboutText();

/**
 * @brief The application window. Hosts an `ads::CDockManager` with one dock widget per area of
 *        the tool.
 *
 * @note Lives on the GUI thread only. Nothing here talks to a PLC: devices, mocks and HIL runs
 *       belong on runner threads and are added by later tasks.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Builds the window, its docks, the menu and the status bar.
     * @param[in] parent Owning widget, usually none.
     */
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /**
     * @brief The docking manager that owns every dock widget.
     * @return Never null.
     */
    ads::CDockManager* dockManager() const;

    /**
     * @brief Titles of the dock widgets, in creation order.
     * @return One title per dock widget.
     */
    QStringList dockTitles() const;

    /**
     * @brief Looks a dock widget up by its title.
     * @param[in] title Title as listed by dockTitles().
     * @return The dock widget, or null when there is none of that title.
     */
    ads::CDockWidget* dockWidget(const QString& title) const;

    /**
     * @brief The content of the "Devices" dock, where device tabs are added.
     * @return Never null.
     */
    DevicePane* devicePane() const { return m_devicePane; }

    /// @brief The content of the "Mock PLCs" dock, where mock tabs are added.
    /// @return Never null.
    MockPane* mockPane() const { return m_mockPane; }

    /// @brief The tabs' trace rings and the shared debug log.
    /// @return Never null.
    TelemetryRegistry* telemetry() const { return m_registry; }

    /// @brief The content of the "Frame trace" dock.
    /// @return Never null.
    TracePane* tracePane() const { return m_tracePane; }

    /// @brief The content of the "Debug log" dock.
    /// @return Never null.
    LogView* logView() const { return m_logView; }

    /// @brief The content of the "HIL runner" dock.
    /// @return Never null.
    HilView* hilView() const { return m_hilView; }

    /// @brief The workspace commands: capture, apply, open, save, recent files.
    /// @return Never null.
    WorkspaceController* workspace() const { return m_workspace; }

    /// @brief Allows or forbids the message boxes that report a refused or failed workspace file
    ///        (the status bar always shows the message). On by default; tests turn it off.
    /// @param[in] on true to show the boxes.
    void setWorkspaceDialogs(bool on) { m_workspaceDialogs = on; }

    /// @brief The last message about a workspace file, as the status bar shows it.
    /// @return Plain text; empty before the first save or load.
    QString workspaceMessage() const { return m_workspaceMessage; }

    /// @brief Asks for a file and loads it (File > Open workspace).
    void openWorkspace();

    /// @brief Saves to the current file, or asks for one when there is none (File > Save workspace).
    void saveWorkspace();

    /// @brief Asks for a file and saves to it (File > Save workspace as).
    void saveWorkspaceAs();

private slots:
    void showAbout();

private:
    void reportWorkspace(bool ok, const QString& title, const QString& message);
    void fillRecentMenu();
    ads::CDockWidget* addDock(const QString& title, QWidget* content);

    ads::CDockManager* m_dockManager;
    qpb::PropertyModel* m_propertyModel;
    DevicePane* m_devicePane{nullptr};
    MockPane* m_mockPane{nullptr};
    TelemetryRegistry* m_registry{nullptr};
    TracePane* m_tracePane{nullptr};
    LogView* m_logView{nullptr};
    HilView* m_hilView{nullptr};
    WorkspaceController* m_workspace{nullptr};
    QMenu* m_recentMenu{nullptr};
    bool m_workspaceDialogs{true};
    QString m_workspaceMessage;
    QStringList m_dockTitles;
};

} // namespace mc::workbench
