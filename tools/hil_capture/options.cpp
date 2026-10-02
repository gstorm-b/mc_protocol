#include "hil_capture/options.h"

#include <QCommandLineOption>
#include <QCommandLineParser>

namespace mc::hil {

namespace {

QCommandLineOption valueOption(const char* name, const char* description, const char* valueName,
                               const QString& def = QString()) {
    return QCommandLineOption(QString::fromLatin1(name), QString::fromLatin1(description),
                              QString::fromLatin1(valueName), def);
}

} // namespace

ParseResult parseOptions(const QStringList& args) {
    ParseResult result;
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Runs a capture plan against one PLC profile and writes the capture set.\n"
        "\n"
        "  hil_capture --profile P.json --plan PLAN.json --dry-run\n"
        "  hil_capture --profile P.json --plan PLAN.json\n"
        "  hil_capture --profile P.json --plan PLAN.json --only G1,G2,G8 --bench-reps 200\n"
        "  hil_capture --report\n"
        "\n"
        "Every write of the run must lie inside the profile's scratch area; the safety gate\n"
        "checks that before anything connects, and no option or plan key switches it off.\n"
        "\n"
        "Exit codes: 0 ok, 1 the run finished but a step failed, 2 bad arguments or an\n"
        "invalid profile or plan, 3 the safety gate refused the run (nothing was sent)."));

    const QCommandLineOption help(QStringList{QStringLiteral("h"), QStringLiteral("help")},
                                  QStringLiteral("Show this help."));
    const QCommandLineOption profile = valueOption("profile", "Profile JSON of the PLC.", "FILE");
    const QCommandLineOption plan = valueOption("plan", "Plan JSON to run.", "FILE");
    const QCommandLineOption dryRun(
        QStringLiteral("dry-run"), QStringLiteral("Resolve the plan, run the safety gate and print "
                                                  "every frame it would send; never connect."));
    const QCommandLineOption only =
        valueOption("only", "Run only these step groups, e.g. G1,G2,G8.", "GROUPS");
    const QCommandLineOption yes(QStringLiteral("yes"),
                                 QStringLiteral("Skip the confirmation prompt (the safety gate "
                                                "still runs; a run with read-only frames is "
                                                "always confirmed)."));
    const QCommandLineOption reps =
        valueOption("bench-reps", "Repetitions of every bench step.", "N", QStringLiteral("200"));
    const QCommandLineOption state =
        valueOption("plc-state", "PLC state the operator set by hand, recorded in the capture.",
                    "RUN|STOP", QStringLiteral("RUN"));
    const QCommandLineOption report(
        QStringLiteral("report"), QStringLiteral("Rebuild the bench report from every bench.csv."));
    const QCommandLineOption root =
        valueOption("output-root",
                    "Folder that receives <profile-id>/ (default tests/vectors/captured, which "
                    "is for hardware captures only).",
                    "DIR", QStringLiteral("tests/vectors/captured"));
    const QCommandLineOption note = valueOption(
        "note", "Operator note recorded in run.meta (e.g. \"virtual_plc fixture, not hardware\").",
        "TEXT");
    const QCommandLineOption reconnect = valueOption(
        "reconnect-timeout", "Seconds a lost link is retried before the run is abandoned.",
        "SECONDS", QStringLiteral("15"));
    const QCommandLineOption reportOut =
        valueOption("report-out", "File that --report writes (default docs/hil/BENCH.md).", "FILE",
                    QStringLiteral("docs/hil/BENCH.md"));
    parser.addOptions({help, profile, plan, dryRun, only, yes, reps, state, report, root, note,
                       reconnect, reportOut});

    if (!parser.parse(args)) {
        result.status = CommandLineStatus::Error;
        result.text = parser.errorText();
        return result;
    }
    if (parser.isSet(help)) {
        result.status = CommandLineStatus::Help;
        result.text = parser.helpText();
        return result;
    }
    if (!parser.positionalArguments().isEmpty()) {
        result.status = CommandLineStatus::Error;
        result.text =
            QStringLiteral("unexpected argument '%1'").arg(parser.positionalArguments().first());
        return result;
    }

    Options& o = result.options;
    o.profilePath = parser.value(profile);
    o.planPath = parser.value(plan);
    o.outputRoot = parser.value(root);
    o.dryRun = parser.isSet(dryRun);
    o.yes = parser.isSet(yes);
    o.report = parser.isSet(report);
    o.note = parser.value(note);
    o.reportOut = parser.value(reportOut);
    for (const QString& g : parser.value(only).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        o.only.push_back(g.trimmed());
    }
    bool ok = false;
    o.benchReps = parser.value(reps).toInt(&ok);
    if (!ok || o.benchReps < 1) {
        result.status = CommandLineStatus::Error;
        result.text = QStringLiteral("--bench-reps is a positive number");
        return result;
    }
    o.reconnectSeconds = parser.value(reconnect).toInt(&ok);
    if (!ok || o.reconnectSeconds < 1) {
        result.status = CommandLineStatus::Error;
        result.text = QStringLiteral("--reconnect-timeout is a positive number of seconds");
        return result;
    }
    o.plcState = parser.value(state).toUpper();
    if (o.plcState != QLatin1String("RUN") && o.plcState != QLatin1String("STOP")) {
        result.status = CommandLineStatus::Error;
        result.text = QStringLiteral("--plc-state is RUN or STOP");
        return result;
    }
    if (!o.report && (o.profilePath.isEmpty() || o.planPath.isEmpty())) {
        result.status = CommandLineStatus::Error;
        result.text = QStringLiteral("--profile and --plan are required (or --report)");
        return result;
    }
    return result;
}

} // namespace mc::hil
