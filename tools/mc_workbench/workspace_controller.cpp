#include "mc_workbench/workspace_controller.h"

#include "mc_workbench/capture_types.h"
#include "mc_workbench/config_binding.h"
#include "mc_workbench/device_tab.h"
#include "mc_workbench/hil_view.h"
#include "mc_workbench/main_window.h"
#include "mc_workbench/mock_tab.h"
#include "mc_workbench/workspace_io.h"

#include <DockManager.h>

#include <QFileInfo>

namespace mc::workbench {

namespace {

CaptureSource sourceFromName(const QString& name) {
    if (name == QLatin1String("plc")) {
        return CaptureSource::RealPlc;
    }
    if (name == QLatin1String("virtual_plc")) {
        return CaptureSource::VirtualPlc;
    }
    return CaptureSource::MockPlc;
}

} // namespace

WorkspaceController::WorkspaceController(MainWindow* window)
    : QObject(window), m_window(window), m_io(new WorkspaceIo(this)) {
    connect(m_io, &WorkspaceIo::loaded, this,
            [this](const QString& path, bool ok, const Workspace& workspace,
                   const QVector<WorkspaceProblem>& problems) {
                onLoaded(path, ok, workspace, problems);
            });
    connect(m_io, &WorkspaceIo::saved, this, [this](const QString& path, bool ok, const QString& message) {
        onSaved(path, ok, message);
    });
}

WorkspaceController::~WorkspaceController() = default;

bool WorkspaceController::busy() const {
    return m_loading || m_saving;
}

Workspace WorkspaceController::capture() const {
    Workspace ws;
    DevicePane* devices = m_window->devicePane();
    for (int i = 0; i < devices->deviceCount(); ++i) {
        if (const DeviceTab* tab = devices->deviceAt(i)) {
            WorkspaceDevice device;
            device.name = tab->name();
            device.config = tab->binding()->config();
            ws.devices.push_back(device);
        }
    }
    MockPane* mocks = m_window->mockPane();
    for (int i = 0; i < mocks->mockCount(); ++i) {
        if (const MockTab* tab = mocks->mockAt(i)) {
            ws.mocks.push_back(tab->saveState());
        }
    }

    const HilView* hil = m_window->hilView();
    ws.hasHil = true;
    ws.hil.profile = hil->profilePath();
    ws.hil.plan = hil->planPath();
    ws.hil.groups = hil->groups();
    ws.hil.plcState = hil->plcState();
    ws.hil.outputRoot = hil->outputRoot();
    ws.hil.source = captureSourceName(hil->source());
    ws.hil.note = hil->note();
    ws.hil.benchReps = hil->benchReps();
    ws.hil.overwrite = hil->overwrite();

    ws.hasLayout = true;
    ws.layout.windowGeometry = m_window->saveGeometry();
    ws.layout.dockState = m_window->dockManager()->saveState();
    ws.layout.currentDevice = devices->currentIndex();
    ws.layout.currentMock = mocks->currentIndex();
    return ws;
}

bool WorkspaceController::apply(const Workspace& workspace, QVector<WorkspaceProblem>* problems,
                                QStringList* warnings) {
    // 1. Build every tab; nothing of the window changes yet.
    QVector<DeviceTab*> newDevices;
    QVector<MockTab*> newMocks;
    QVector<WorkspaceProblem> found;
    for (int i = 0; i < workspace.devices.size(); ++i) {
        const WorkspaceDevice& entry = workspace.devices.at(i);
        auto* tab = new DeviceTab(entry.name);
        newDevices.push_back(tab);
        QString error;
        if (!tab->setConfig(entry.config, &error)) {
            found.push_back({QStringLiteral("devices[%1].config").arg(i), error});
        }
    }
    for (int i = 0; i < workspace.mocks.size(); ++i) {
        const WorkspaceMock& entry = workspace.mocks.at(i);
        auto* tab = new MockTab(entry.name);
        newMocks.push_back(tab);
        tab->loadState(entry, QStringLiteral("mocks[%1]").arg(i), &found);
    }
    if (!found.isEmpty()) {
        qDeleteAll(newDevices);
        qDeleteAll(newMocks);
        if (problems != nullptr) {
            *problems += found;
        }
        return false;
    }

    // 2. Commit: the new tabs go in, the old ones are closed (each stops its runner thread).
    DevicePane* devicePane = m_window->devicePane();
    MockPane* mockPane = m_window->mockPane();
    const int oldDevices = devicePane->deviceCount();
    const int oldMocks = mockPane->mockCount();
    for (DeviceTab* tab : newDevices) {
        devicePane->adoptDevice(tab);
    }
    for (MockTab* tab : newMocks) {
        mockPane->adoptMock(tab);
    }
    for (int i = 0; i < oldDevices; ++i) {
        devicePane->closeDevice(0);
    }
    for (int i = 0; i < oldMocks; ++i) {
        mockPane->closeMock(0);
    }

    if (workspace.hasHil) {
        HilView* hil = m_window->hilView();
        const WorkspaceHil& h = workspace.hil;
        hil->setProfilePath(h.profile);
        hil->setPlanPath(h.plan);
        hil->setGroups(h.groups);
        hil->setPlcState(h.plcState);
        hil->setOutputRoot(h.outputRoot);
        hil->setNote(h.note);
        hil->setBenchReps(h.benchReps);
        hil->setOverwrite(h.overwrite);
        hil->setSource(sourceFromName(h.source));
    }

    if (workspace.hasLayout) {
        const WorkspaceLayout& l = workspace.layout;
        if (!l.dockState.isEmpty()) {
            ads::CDockManager* manager = m_window->dockManager();
            const QByteArray before = manager->saveState();
            if (!manager->restoreState(l.dockState)) {
                manager->restoreState(before);
                if (warnings != nullptr) {
                    warnings->append(QStringLiteral("the dock layout could not be restored"));
                }
            }
        }
        if (!l.windowGeometry.isEmpty() && !m_window->restoreGeometry(l.windowGeometry) &&
            warnings != nullptr) {
            warnings->append(QStringLiteral("the window geometry could not be restored"));
        }
        devicePane->setCurrentIndex(l.currentDevice);
        mockPane->setCurrentIndex(l.currentMock);
    } else {
        devicePane->setCurrentIndex(0);
        mockPane->setCurrentIndex(0);
    }
    return true;
}

bool WorkspaceController::save(const QString& path) {
    if (busy()) {
        return false;
    }
    m_saving = true;
    m_io->save(path, capture());
    return true;
}

bool WorkspaceController::load(const QString& path) {
    if (busy()) {
        return false;
    }
    m_loading = true;
    m_io->load(path);
    return true;
}

void WorkspaceController::setPath(const QString& path) {
    m_path = QFileInfo(path).absoluteFilePath();
    m_recent.add(m_path);
    m_window->setWindowTitle(QStringLiteral("MC Workbench - %1").arg(QFileInfo(path).fileName()));
}

void WorkspaceController::onSaved(const QString& path, bool ok, const QString& message) {
    m_saving = false;
    if (ok) {
        setPath(path);
        emit saveFinished(path, true, QStringLiteral("Workspace saved to %1").arg(path));
    } else {
        emit saveFinished(path, false, message);
    }
}

void WorkspaceController::onLoaded(const QString& path, bool ok, const Workspace& workspace,
                                   const QVector<WorkspaceProblem>& problems) {
    QVector<WorkspaceProblem> found = problems;
    QStringList warnings;
    bool applied = false;
    if (ok) {
        applied = apply(workspace, &found, &warnings);
    }
    m_loading = false;
    if (applied) {
        setPath(path);
        QString message = QStringLiteral("Workspace loaded from %1 (%2 device tabs, %3 mock tabs)")
                              .arg(path)
                              .arg(workspace.devices.size())
                              .arg(workspace.mocks.size());
        if (!warnings.isEmpty()) {
            message += QStringLiteral("; ") + warnings.join(QStringLiteral("; "));
        }
        emit loadFinished(path, true, message);
        return;
    }
    if (!QFileInfo::exists(path)) {
        m_recent.remove(QFileInfo(path).absoluteFilePath());
    }
    emit loadFinished(path, false,
                      QStringLiteral("Workspace refused: %1\n%2\nNothing was changed.")
                          .arg(path, workspaceProblemsText(found)));
}

} // namespace mc::workbench
