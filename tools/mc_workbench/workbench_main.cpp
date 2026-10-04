// mc_workbench: the Qt Widgets tool of SPEC-gui-tool.md. All the work is in the library parts of
// this folder; this file only starts the application and shows the main window. It is not called
// main.cpp on purpose: tools/qmake builds hil_capture (a main.cpp of its own) beside it, and jom picks
// the first main.cpp its inference rules find for main.obj (T-074).
#include "mc_workbench/main_window.h"
#include "mc_workbench/workspace_controller.h"

#include <QApplication>
#include <QStandardPaths>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MC Workbench"));

    mc::workbench::MainWindow window;
    // The list of recent workspace files (paths only) lives in the user's configuration folder, or in
    // the folder named by the environment variable MC_WORKBENCH_CONFIG_DIR (the tests point it into the
    // build tree). The folder is created when the list is first saved, not at start.
    QString configDir = qEnvironmentVariable("MC_WORKBENCH_CONFIG_DIR");
    if (configDir.isEmpty()) {
        configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (!configDir.isEmpty()) {
        window.workspace()->recent().setStorePath(configDir + QStringLiteral("/recent_workspaces.ini"));
    }
    window.show();
    // A workspace file named on the command line is opened at once.
    const QStringList arguments = QApplication::arguments();
    if (arguments.size() > 1) {
        window.workspace()->load(arguments.at(1));
    }
    return app.exec();
}
