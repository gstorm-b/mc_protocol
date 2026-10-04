// GUI-09 (smoke): the main window opens offscreen with ADS and qpb. One suite of mc_workbench_tests;
// the main() in gui_tests_main.cpp runs it next to the others.
#include "gui_suites.h"
#include "mc_workbench/component_versions.h"
#include "mc_workbench/main_window.h"

#include <DockManager.h>
#include <DockWidget.h>

#include <qpb/widgets/PropertyTreeView.h>

#include <QMenuBar>
#include <QStatusBar>
#include <QStringList>
#include <QTest>

#include <memory>

namespace {

const QStringList kExpectedDocks = {
    QStringLiteral("Devices"),    QStringLiteral("Mock PLCs"),   QStringLiteral("HIL runner"),
    QStringLiteral("Frame trace"), QStringLiteral("Debug log"),  QStringLiteral("Properties"),
};

} // namespace

class GuiSmokeTest : public QObject {
    Q_OBJECT

private slots:
    // GUI-09: the main window opens offscreen with ADS and qpb, every dock widget is created.
    void GUI_09_mainWindowOpensWithEveryDock() {
        auto window = std::make_unique<mc::workbench::MainWindow>();
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));

        QVERIFY(window->dockManager() != nullptr);
        QCOMPARE(window->dockTitles(), kExpectedDocks);
        QCOMPARE(window->dockManager()->dockWidgetsMap().size(), kExpectedDocks.size());
        for (const QString& title : kExpectedDocks) {
            ads::CDockWidget* dock = window->dockWidget(title);
            QVERIFY2(dock != nullptr, qPrintable(title));
            QVERIFY2(dock->widget() != nullptr, qPrintable(title));
        }

        // The qpb grid is a real child of its dock and lists the four component versions.
        ads::CDockWidget* properties = window->dockWidget(QStringLiteral("Properties"));
        auto* view = qobject_cast<qpb::PropertyTreeView*>(properties->widget());
        QVERIFY(view != nullptr);
        QCOMPARE(view->model()->rowCount(), 4); // the root group is not a row

        QVERIFY(window->menuBar()->actions().size() >= 3);
        QCOMPARE(window->statusBar()->currentMessage(), QStringLiteral("Ready"));

        // The window closes cleanly, which is what lets the program exit with code 0.
        QVERIFY(window->close());
        window.reset();
    }

    // GUI-09: the About text names the version of every component the tool is built from.
    void GUI_09_aboutTextListsEveryComponent() {
        const mc::workbench::ComponentVersions versions = mc::workbench::componentVersions();
        QVERIFY(!versions.mc.isEmpty());
        QVERIFY(!versions.qt.isEmpty());
        QVERIFY(!versions.qpb.isEmpty());

        const QString text = mc::workbench::aboutText();
        QVERIFY(text.contains(versions.mc));
        QVERIFY(text.contains(versions.qt));
        QVERIFY(text.contains(versions.qpb));
        QVERIFY(text.contains(QStringLiteral("Advanced Docking System 5.")));
    }

    // GUI-09: several windows can exist one after another in one process (the dock manager keeps
    // no global state that a second window trips over).
    void GUI_09_windowCanBeCreatedTwice() {
        for (int i = 0; i < 2; ++i) {
            mc::workbench::MainWindow window;
            window.show();
            QCOMPARE(window.dockTitles().size(), kExpectedDocks.size());
        }
    }
};

namespace mc::workbench::test {

QObject* makeSmokeSuite() {
    return new GuiSmokeTest;
}

} // namespace mc::workbench::test

#include "tst_gui_smoke.moc"
