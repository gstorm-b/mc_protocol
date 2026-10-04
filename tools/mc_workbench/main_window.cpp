#include "mc_workbench/main_window.h"

#include "mc_workbench/component_versions.h"
#include "mc_workbench/device_tab.h"
#include "mc_workbench/hil_view.h"
#include "mc_workbench/log_view.h"
#include "mc_workbench/mock_tab.h"
#include "mc_workbench/tab_telemetry.h"
#include "mc_workbench/trace_pane.h"
#include "mc_workbench/workspace_controller.h"

#include <ads_version.h>
#include <DockManager.h>
#include <DockWidget.h>

#include <qpb/PropertyGroup.h>
#include <qpb/PropertyModel.h>
#include <qpb/widgets/PropertyTreeView.h>

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

#include <memory>

namespace mc::workbench {

namespace {

constexpr const char* kTitleDevices = "Devices";
constexpr const char* kTitleMocks = "Mock PLCs";
constexpr const char* kTitleTrace = "Frame trace";
constexpr const char* kTitleLog = "Debug log";
constexpr const char* kTitleHil = "HIL runner";
constexpr const char* kTitleProperties = "Properties";

QString adsVersionString() {
    return QStringLiteral("%1.%2.%3")
        .arg(ADS_VERSION_MAJOR)
        .arg(ADS_VERSION_MINOR)
        .arg(ADS_VERSION_PATCH);
}

} // namespace

QString aboutText() {
    const ComponentVersions versions = componentVersions();
    return QStringLiteral("MC Workbench\n\n"
                          "mc_protocol %1\n"
                          "Qt %2\n"
                          "Qt Advanced Docking System %3 (LGPL-2.1)\n"
                          "qpb %4 (MIT)")
        .arg(versions.mc, versions.qt, adsVersionString(), versions.qpb);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_dockManager(nullptr), m_propertyModel(nullptr) {
    setWindowTitle(QStringLiteral("MC Workbench"));
    resize(1200, 800);

    ads::CDockManager::setConfigFlags(ads::CDockManager::DefaultOpaqueConfig);
    m_dockManager = new ads::CDockManager(this);

    // Properties: a qpb grid; for now it lists the component versions, read only.
    const ComponentVersions versions = componentVersions();
    auto root = qpb::PropertyGroup::create(QStringLiteral("Workbench"));
    root->addString(QStringLiteral("mc"), versions.mc).displayName(QStringLiteral("mc_protocol")).readOnly();
    root->addString(QStringLiteral("qt"), versions.qt).displayName(QStringLiteral("Qt")).readOnly();
    root->addString(QStringLiteral("ads"), adsVersionString())
        .displayName(QStringLiteral("Docking system"))
        .readOnly();
    root->addString(QStringLiteral("qpb"), versions.qpb).displayName(QStringLiteral("qpb")).readOnly();
    m_propertyModel = new qpb::PropertyModel(std::move(root), this);
    auto* propertyView = new qpb::PropertyTreeView;
    propertyView->setModel(m_propertyModel);

    m_registry = new TelemetryRegistry(this);
    m_devicePane = new DevicePane;
    m_devicePane->setRegistry(m_registry);
    m_mockPane = new MockPane;
    m_mockPane->setRegistry(m_registry);
    m_tracePane = new TracePane(m_registry);
    m_logView = new LogView(m_registry->log());
    m_hilView = new HilView;
    ads::CDockWidget* devices = addDock(QString::fromLatin1(kTitleDevices), m_devicePane);
    ads::CDockWidget* mocks = addDock(QString::fromLatin1(kTitleMocks), m_mockPane);
    ads::CDockWidget* hil =
        addDock(QString::fromLatin1(kTitleHil), m_hilView);
    ads::CDockWidget* trace = addDock(QString::fromLatin1(kTitleTrace), m_tracePane);
    ads::CDockWidget* log = addDock(QString::fromLatin1(kTitleLog), m_logView);
    ads::CDockWidget* properties = addDock(QString::fromLatin1(kTitleProperties), propertyView);

    ads::CDockAreaWidget* centerArea = m_dockManager->addDockWidget(ads::CenterDockWidgetArea, devices);
    m_dockManager->addDockWidgetTabToArea(mocks, centerArea);
    m_dockManager->addDockWidgetTabToArea(hil, centerArea);
    ads::CDockAreaWidget* bottomArea = m_dockManager->addDockWidget(ads::BottomDockWidgetArea, trace);
    m_dockManager->addDockWidgetTabToArea(log, bottomArea);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, properties);
    devices->setAsCurrentTab();
    trace->setAsCurrentTab();

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    QAction* newDevice = fileMenu->addAction(QStringLiteral("New &device tab"));
    newDevice->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(newDevice, &QAction::triggered, this, [this]() {
        m_devicePane->addDevice();
        dockWidget(QString::fromLatin1(kTitleDevices))->toggleView(true);
    });
    fileMenu->addSeparator();
    QAction* openWs = fileMenu->addAction(QStringLiteral("&Open workspace..."));
    openWs->setShortcut(QKeySequence::Open);
    connect(openWs, &QAction::triggered, this, &MainWindow::openWorkspace);
    QAction* saveWs = fileMenu->addAction(QStringLiteral("&Save workspace"));
    saveWs->setShortcut(QKeySequence::Save);
    connect(saveWs, &QAction::triggered, this, &MainWindow::saveWorkspace);
    QAction* saveWsAs = fileMenu->addAction(QStringLiteral("Save workspace &as..."));
    saveWsAs->setShortcut(QKeySequence::SaveAs);
    connect(saveWsAs, &QAction::triggered, this, &MainWindow::saveWorkspaceAs);
    m_recentMenu = fileMenu->addMenu(QStringLiteral("&Recent workspaces"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::fillRecentMenu);
    fileMenu->addSeparator();
    QAction* quit = fileMenu->addAction(QStringLiteral("E&xit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    for (const QString& title : m_dockTitles) {
        viewMenu->addAction(dockWidget(title)->toggleViewAction());
    }

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    QAction* about = helpMenu->addAction(QStringLiteral("&About MC Workbench"));
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);

    m_workspace = new WorkspaceController(this);
    connect(m_workspace, &WorkspaceController::saveFinished, this,
            [this](const QString&, bool ok, const QString& message) {
                reportWorkspace(ok, QStringLiteral("Save workspace"), message);
            });
    connect(m_workspace, &WorkspaceController::loadFinished, this,
            [this](const QString&, bool ok, const QString& message) {
                reportWorkspace(ok, QStringLiteral("Open workspace"), message);
            });

    statusBar()->showMessage(QStringLiteral("Ready"));
}

MainWindow::~MainWindow() = default;

ads::CDockManager* MainWindow::dockManager() const {
    return m_dockManager;
}

QStringList MainWindow::dockTitles() const {
    return m_dockTitles;
}

ads::CDockWidget* MainWindow::dockWidget(const QString& title) const {
    return m_dockManager->findDockWidget(title);
}

void MainWindow::reportWorkspace(bool ok, const QString& title, const QString& message) {
    m_workspaceMessage = message;
    statusBar()->showMessage(message.section(QLatin1Char('\n'), 0, 0));
    if (ok || !m_workspaceDialogs) {
        return;
    }
    // Window-modal but not blocking: the box is opened, not exec()ed.
    auto* box = new QMessageBox(QMessageBox::Warning, title, message, QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

void MainWindow::openWorkspace() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open workspace"), m_workspace->path(),
        QStringLiteral("MC Workbench workspace (*.json);;All files (*)"));
    if (!path.isEmpty() && !m_workspace->load(path)) {
        reportWorkspace(false, QStringLiteral("Open workspace"),
                        QStringLiteral("Another workspace file operation is still running."));
    }
}

void MainWindow::saveWorkspace() {
    if (m_workspace->path().isEmpty()) {
        saveWorkspaceAs();
        return;
    }
    if (!m_workspace->save(m_workspace->path())) {
        reportWorkspace(false, QStringLiteral("Save workspace"),
                        QStringLiteral("Another workspace file operation is still running."));
    }
}

void MainWindow::saveWorkspaceAs() {
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save workspace as"), m_workspace->path(),
        QStringLiteral("MC Workbench workspace (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".json");
    }
    if (!m_workspace->save(path)) {
        reportWorkspace(false, QStringLiteral("Save workspace"),
                        QStringLiteral("Another workspace file operation is still running."));
    }
}

void MainWindow::fillRecentMenu() {
    m_recentMenu->clear();
    const QStringList paths = m_workspace->recent().paths();
    for (const QString& path : paths) {
        QAction* action = m_recentMenu->addAction(path);
        connect(action, &QAction::triggered, this, [this, path]() {
            if (!m_workspace->load(path)) {
                reportWorkspace(false, QStringLiteral("Open workspace"),
                                QStringLiteral("Another workspace file operation is still running."));
            }
        });
    }
    if (paths.isEmpty()) {
        m_recentMenu->addAction(QStringLiteral("(none)"))->setEnabled(false);
    }
}

void MainWindow::showAbout() {
    QMessageBox::about(this, QStringLiteral("About MC Workbench"), aboutText());
}

ads::CDockWidget* MainWindow::addDock(const QString& title, QWidget* content) {
    auto* dock = new ads::CDockWidget(m_dockManager, title);
    dock->setWidget(content);
    m_dockTitles.append(title);
    return dock;
}

} // namespace mc::workbench
