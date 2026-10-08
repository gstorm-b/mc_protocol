// hil_capture: runs a capture plan against one PLC profile (SPEC-hil-capture.md). All the work is
// in tool.cpp; this file only turns the process arguments into a call.
#include "hil_capture/options.h"
#include "hil_capture/tool.h"

#include <QCoreApplication>
#include <QTextStream>

#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);
    QTextStream in(stdin);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt 6 streams are UTF-8; Qt 5 ones use the locale's codec (the ANSI code page on Windows),
    // which loses the characters of a non-ASCII path or note. Same text on both majors.
    for (QTextStream* stream : {&out, &err, &in}) {
        stream->setCodec("UTF-8");
    }
#endif

    const mc::hil::ParseResult parsed = mc::hil::parseOptions(app.arguments());
    switch (parsed.status) {
    case mc::hil::CommandLineStatus::Help:
        out << parsed.text;
        out.flush();
        return static_cast<int>(mc::hil::ExitCode::Ok);
    case mc::hil::CommandLineStatus::Error:
        err << "hil_capture: " << parsed.text << "\n";
        err.flush();
        return static_cast<int>(mc::hil::ExitCode::BadInput);
    case mc::hil::CommandLineStatus::Run:
        break;
    }
    const mc::hil::ToolIo io{&out, &err, &in};
    const mc::hil::ExitCode code = mc::hil::runTool(parsed.options, io);
    out.flush();
    err.flush();
    return static_cast<int>(code);
}
