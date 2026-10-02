// Helpers shared by the suites of mc_hil_tool_tests.
#pragma once

#include "hil_capture/options.h"
#include "hil_capture/plan.h"
#include "hil_capture/profile.h"
#include "hil_capture/tool.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>

#include <functional>

namespace mc::hil::test {

/// tests/ of the source tree (compile definition MC_TESTS_SOURCE_DIR, as the other test binaries).
inline QString testsDir() { return QStringLiteral(MC_TESTS_SOURCE_DIR); }

/// A folder under the build tree for this test's files (captures made against
/// virtual_plc or the mock never go to tests/vectors/captured/). Emptied on every call.
inline QString scratchDir(const QString& name) {
    QDir dir(QStringLiteral(MC_HIL_OUTPUT_DIR) + QLatin1Char('/') + name);
    dir.removeRecursively();
    QDir().mkpath(dir.absolutePath());
    return dir.absolutePath();
}

/// tests/hil/profiles/<id>.example.json
inline QString exampleProfilePath(const QString& id) {
    return testsDir() + QStringLiteral("/hil/profiles/") + id + QStringLiteral(".example.json");
}

/// Reads a JSON object from a file; an empty object (and a test failure upstream) otherwise.
inline QJsonObject readJsonFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(f.readAll()).object();
}

/// Writes a JSON object to a file; false when the file cannot be written.
inline bool writeJsonFile(const QString& path, const QJsonObject& obj) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(QJsonDocument(obj).toJson());
    return true;
}

/// Parses JSON text; an empty object when it is not an object.
inline QJsonObject parseJson(const char* text) {
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

/// The example profile with that id, loaded (a default Profile on failure).
inline Profile loadExample(const QString& id) {
    const ProfileLoad load = loadProfileFile(exampleProfilePath(id));
    return load.ok() ? *load.profile : Profile();
}

/// What a call of runTool() printed and returned.
struct ToolRun {
    ExitCode code{ExitCode::Ok};
    QString out;
    QString err;
};

/// Runs the tool in this process with its streams captured; @p input is what the operator types.
inline ToolRun runToolWith(const Options& options, const QString& input = QString()) {
    ToolRun run;
    QString outText;
    QString errText;
    QString inText = input;
    QTextStream out(&outText);
    QTextStream err(&errText);
    QTextStream in(&inText);
    run.code = runTool(options, ToolIo{&out, &err, &in});
    out.flush();
    err.flush();
    run.out = outText;
    run.err = errText;
    return run;
}

} // namespace mc::hil::test
