// HIL-02 and HIL-03 (SPEC-hil-capture.md): the safety gate refuses every write outside the scratch
// area, lists every offender and sends nothing; --dry-run prints exactly the frames McProtocol
// encodes. No hardware.
#include "hil_suites.h"
#include "hil_test_support.h"

#include "hil_capture/dry_run.h"
#include "hil_capture/options.h"
#include "hil_capture/plan.h"
#include "hil_capture/profile.h"
#include "hil_capture/resolve.h"
#include "hil_capture/safety_gate.h"
#include "hil_capture/tool.h"
#include "mc/core/poll_plan.h"
#include "mc/core/protocol.h"
#include "mc/mock/mock_plc.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QTcpServer>
#include <QtTest>

namespace mc::hil::test {

namespace {

QByteArray planWith(const QByteArray& steps) {
    return QByteArray(R"JSON({"schema":1,"plan":{"id":"gate"},"steps":[)JSON") + steps + "]}";
}

struct Verdict {
    ResolveResult resolved;
    GateReport gate;
    QString planError;
};

Verdict gateOf(const QByteArray& steps, const Profile& profile) {
    Verdict v;
    const PlanLoad plan = loadPlan(QJsonDocument::fromJson(planWith(steps)).object());
    if (!plan.ok()) {
        v.planError = plan.error.text();
        return v;
    }
    v.resolved = resolvePlan(*plan.plan, profile);
    v.gate = checkGate(v.resolved, profile);
    return v;
}

QStringList stepsOf(const GateReport& g) {
    QStringList ids;
    for (const GateViolation& v : g.violations) {
        ids << v.stepId;
    }
    return ids;
}

// What the gate says about one step: the kind of every violation, in order.
QStringList kindsOf(const GateReport& g, const QString& stepId) {
    QStringList kinds;
    for (const GateViolation& v : g.violations) {
        if (v.stepId == stepId) {
            kinds << v.kind;
        }
    }
    return kinds;
}

Profile qProfile() { return loadExample(QStringLiteral("q03ude-eth-3e-bin")); }

// The example Q profile aimed at 127.0.0.1:<port>; returns the file.
QString writeLoopbackProfile(const QString& dir, quint16 port) {
    QJsonObject root = readJsonFile(exampleProfilePath(QStringLiteral("q03ude-eth-3e-bin")));
    QJsonObject device = root.value(QStringLiteral("device")).toObject();
    QJsonObject transport = device.value(QStringLiteral("transport")).toObject();
    QJsonObject tcp = transport.value(QStringLiteral("tcp")).toObject();
    tcp.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    tcp.insert(QStringLiteral("port"), static_cast<int>(port));
    transport.insert(QStringLiteral("tcp"), tcp);
    device.insert(QStringLiteral("transport"), transport);
    root.insert(QStringLiteral("device"), device);
    const QString path = dir + QStringLiteral("/profile.json");
    writeJsonFile(path, root);
    return path;
}

// The example profile @p id, with `scratch` replaced.
Profile withScratch(const QStringList& ranges,
                    const QString& id = QStringLiteral("q03ude-eth-3e-bin")) {
    QJsonObject root = readJsonFile(exampleProfilePath(id));
    QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
    profile.insert(QStringLiteral("scratch"), QJsonArray::fromStringList(ranges));
    root.insert(QStringLiteral("profile"), profile);
    return *loadProfile(root).profile;
}

QString hexOf(const ByteBuf& b) { return hexText(b); }

// One raw step as compact JSON (the frames are built here, so they are not typed into a literal).
QByteArray rawStepJson(const QString& id, const QString& hex) {
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("kind"), QStringLiteral("raw"));
    o.insert(QStringLiteral("hex"), hex);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QStringList txLines(const QString& text) {
    static const QRegularExpression re(QStringLiteral(R"(^ {4}tx ((?:[0-9A-F]{2} ?)+)$)"),
                                       QRegularExpression::MultilineOption);
    QStringList lines;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        lines << it.next().captured(1).trimmed();
    }
    return lines;
}

ByteBuf wordBytes(std::initializer_list<uint16_t> words) {
    ByteBuf b;
    for (const uint16_t w : words) {
        b.push_back(static_cast<uint8_t>(w & 0xFF));
        b.push_back(static_cast<uint8_t>(w >> 8));
    }
    return b;
}

// ---- The read-only command allow-list: frames built by hand
// ---------------------------------------
//
// The gate's mock cannot decode these commands, so only the allow-list can tell a read from a
// write. Every frame is written out here from the reference spec (sections 4 and 5), not taken from
// the library's encoder, which has no way to build 1402, 1E 04H/05H or 1C BT/WT.

QString spaced(const QByteArray& bytes) { return QString::fromLatin1(bytes.toHex(' ').toUpper()); }

QByteArray ascii(const char* text) { return QByteArray(text); }

// 3E Binary around a request body (command, subcommand, request data).
QByteArray frame3EBin(const QByteArray& body) {
    QByteArray f = QByteArray::fromHex("500000FFFF0300");
    const int length = 2 + static_cast<int>(body.size());
    f.append(static_cast<char>(length & 0xFF));
    f.append(static_cast<char>(length >> 8));
    f.append(QByteArray::fromHex("1000"));
    return f + body;
}

// 4E Binary (serial No. 1234H) around a request body.
QByteArray frame4EBin(const QByteArray& body) {
    QByteArray f = QByteArray::fromHex("54003412"
                                       "0000"
                                       "00FFFF0300");
    const int length = 2 + static_cast<int>(body.size());
    f.append(static_cast<char>(length & 0xFF));
    f.append(static_cast<char>(length >> 8));
    f.append(QByteArray::fromHex("1000"));
    return f + body;
}

// 3E or 4E ASCII around a request body written as text.
QByteArray frameQnaAscii(bool fourE, const QByteArray& body) {
    const QByteArray length =
        QByteArray::number(4 + static_cast<int>(body.size()), 16).rightJustified(4, '0').toUpper();
    return (fourE ? ascii("540012340000") : ascii("5000")) + ascii("00FF03FF00") + length +
           ascii("0010") + body;
}

// The request bodies, Binary (command and subcommand little endian, as in section 4.1).
QByteArray qnaBodyBin(const QString& command) {
    static const QMap<QString, QByteArray> bodies{{"0401", QByteArray::fromHex("01040000"
                                                                               "640000A8"
                                                                               "0100")},
                                                  {"0403", QByteArray::fromHex("03040000"
                                                                               "0100"
                                                                               "640000A8")},
                                                  {"0406", QByteArray::fromHex("06040000"
                                                                               "0100"
                                                                               "640000A8"
                                                                               "0100")},
                                                  {"0801", QByteArray::fromHex("01080000"
                                                                               "0100"
                                                                               "640000A8")},
                                                  {"0619", QByteArray::fromHex("19060000"
                                                                               "0200"
                                                                               "4142")},
                                                  {"1401", QByteArray::fromHex("01140000"
                                                                               "640000A8"
                                                                               "0100"
                                                                               "0700")},
                                                  {"1402", QByteArray::fromHex("02140000"
                                                                               "0100"
                                                                               "320000A8"
                                                                               "0700")},
                                                  {"1406", QByteArray::fromHex("06140000"
                                                                               "0100"
                                                                               "640000A8"
                                                                               "0100"
                                                                               "0700")}};
    return bodies.value(command);
}

// The same, ASCII: the command text, the subcommand and some request data.
QByteArray qnaBodyAscii(const QString& command) {
    return command.toLatin1() + ascii("0000D*0000640001");
}

// 1E Binary: command (the subheader), PC No., timer, then the request data of section 4.2.
QByteArray frame1EBin(unsigned command, bool write) {
    QByteArray f;
    f.append(static_cast<char>(command));
    f += QByteArray::fromHex("FF0A00");
    if (write) {
        f += QByteArray::fromHex("010032000000"
                                 "2044"
                                 "0700"); // n = 1, D50, one word
    } else {
        f += QByteArray::fromHex("64000000"
                                 "2044"
                                 "0100"); // D100 x 1 (12 bytes in all)
    }
    return f;
}

// 1E ASCII: the same in characters (a read is 24 of them).
QByteArray frame1EAscii(unsigned command, bool write) {
    const QByteArray head =
        QByteArray::number(static_cast<int>(command), 16).rightJustified(2, '0').toUpper() +
        ascii("FF000A");
    return head + (write ? ascii("0100"
                                 "4420"
                                 "00000032"
                                 "0007")
                         : ascii("4420"
                                 "00000064"
                                 "0100"));
}

// A serial frame of format 1-4 around the request data (the route and the sum are placed as the
// reference spec's section 5.4-5.6 table shows; the sum value is not checked by the gate).
// The route is written out: "00FF" (1C), "F90000FF00" (3C), "F80000FF03FF0000" (4C).
QByteArray serialFrameRoute(int format, const QByteArray& route, const QByteArray& requestData) {
    const QByteArray sum = ascii("00");
    switch (format) {
    case 2:
        return QByteArray(1, '\x05') + ascii("00") + route + requestData + sum;
    case 3:
        return QByteArray(1, '\x02') + route + requestData + QByteArray(1, '\x03') + sum;
    case 4:
        return QByteArray(1, '\x05') + route + requestData + sum + ascii("\r\n");
    default:
        return QByteArray(1, '\x05') + route + requestData + sum;
    }
}

QByteArray serialFrame(int format, bool oneC, const QByteArray& requestData) {
    return serialFrameRoute(format, oneC ? ascii("00FF") : ascii("F90000FF00"), requestData);
}

// A 4C frame (frame id F8, a 14 character route) of format 1-4.
QByteArray frame4C(int format, const QByteArray& requestData) {
    return serialFrameRoute(format, ascii("F80000FF03FF0000"), requestData);
}

// The example profile with some FrameConfig keys changed (a format, a data code).
Profile withFrameKeys(const QString& example, const QJsonObject& keys) {
    QJsonObject root = readJsonFile(exampleProfilePath(example));
    QJsonObject device = root.value(QStringLiteral("device")).toObject();
    QJsonObject frame = device.value(QStringLiteral("frame")).toObject();
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        frame.insert(it.key(), it.value());
    }
    device.insert(QStringLiteral("frame"), frame);
    root.insert(QStringLiteral("device"), device);
    const ProfileLoad load = loadProfile(root);
    return load.ok() ? *load.profile : Profile();
}

// The profile a table row names: "3E-bin", "3E-ascii", "1E-bin", "1E-ascii", "3C-F1".."3C-F4",
// "1C-F1".."1C-F4", "1C-ana" (the AnA/AnU command set, format 1).
Profile profileForRow(const QString& key) {
    const auto keys = [](const char* format, const char* code, const char* commandSet = "ACPU") {
        QJsonObject o;
        o.insert(QStringLiteral("format"), QLatin1String(format));
        o.insert(QStringLiteral("code"), QLatin1String(code));
        o.insert(QStringLiteral("commandSet"), QLatin1String(commandSet));
        return o;
    };
    if (key == QLatin1String("3E-bin")) {
        return loadExample(QStringLiteral("q03ude-eth-3e-bin"));
    }
    if (key == QLatin1String("3E-ascii")) {
        return loadExample(QStringLiteral("fx5u-eth-3e-ascii"));
    }
    if (key == QLatin1String("1E-bin")) {
        return loadExample(QStringLiteral("fx3-eth-1e-bin"));
    }
    if (key == QLatin1String("1E-ascii")) {
        QJsonObject o;
        o.insert(QStringLiteral("code"), QStringLiteral("Ascii"));
        return withFrameKeys(QStringLiteral("fx3-eth-1e-bin"), o);
    }
    if (key == QLatin1String("1C-ana")) {
        return withFrameKeys(QStringLiteral("fx3-serial-1c-f1"), keys("Format1", "Ascii", "AnA"));
    }
    const QString example = key.startsWith(QLatin1String("3C"))
                                ? QStringLiteral("q03ude-c24-3c-f4")
                                : QStringLiteral("fx3-serial-1c-f1");
    return withFrameKeys(example,
                         keys(qPrintable(QStringLiteral("Format") + key.right(1)), "Ascii"));
}

QByteArray readOnlyRawJson(const QString& id, const QString& hex, const char* recover) {
    QJsonObject o = QJsonDocument::fromJson(rawStepJson(id, hex)).object();
    o.insert(QStringLiteral("readOnly"), true);
    o.insert(QStringLiteral("recover"), QLatin1String(recover));
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Whether any violation of the step is the allow-list's, and what it names.
QString allowListWhy(const GateReport& g, const QString& stepId) {
    for (const GateViolation& v : g.violations) {
        if (v.stepId == stepId && v.kind.endsWith(QStringLiteral("(declared readOnly)")) &&
            v.range.startsWith(QStringLiteral("command "))) {
            return v.range + QLatin1Char('|') + v.why;
        }
    }
    return QString();
}

// A step as JSON with a frameOverride added.
QByteArray withOverride(const QByteArray& stepJson, const char* overrideJson) {
    QJsonObject o = QJsonDocument::fromJson(stepJson).object();
    o.insert(QStringLiteral("frameOverride"), QJsonDocument::fromJson(overrideJson).object());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// "<code>@<byte>" of every violation that says a command is not accounted for in a step.
QStringList unaccounted(const GateReport& g, const QString& stepId) {
    QStringList found;
    for (const GateViolation& v : g.violations) {
        if (v.stepId == stepId && v.kind.endsWith(QStringLiteral("(command not accounted for)"))) {
            found << v.range;
        }
    }
    return found;
}

class HilGateTests : public QObject {
    Q_OBJECT

  private slots:
    // ---- HIL-02: what the gate refuses --------------------------------------------------------

    void HIL_02_writesInsideScratchPass() {
        const Verdict v = gateOf(R"JSON(
            {"id":"OK-01","kind":"write","device":"D@s","values":[1,2,3]},
            {"id":"OK-02","kind":"write","device":"M@s16","unit":"word","count":2,"values":[1,2]},
            {"id":"OK-03","kind":"write","device":"W@s","values":[1]},
            {"id":"OK-04","kind":"read","device":"D3000"},
            {"id":"OK-05","kind":"read","device":"X0","count":16},
            {"id":"OK-06","kind":"write","device":"D100","count":2000,"values":{"gen":"index",
                "mul":1}}
        )JSON",
                                 qProfile());
        QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
        QVERIFY2(!v.gate.refused(), qPrintable(refusalText(v.gate)));
    }

    // XYN: the gate counts indices, and its texts use the profile's notation.
    void HIL_XYN_05_theGateChecksIndicesAndNamesRangesInOctal() {
        const Profile fx = withScratch({"D100-D2099", "M100-M2099", "Y20-Y37"},
                                       QStringLiteral("fx5u-eth-3e-ascii")); // Y20-Y37 = 16..31
        const Verdict ok = gateOf(R"JSON(
            {"id":"OK-01","kind":"write","device":"Y@s","unit":"bit","count":16,
                "values":{"gen":"fill","value":1}},
            {"id":"OK-02","kind":"write","device":"Y30","unit":"bit","count":8,
                "values":{"gen":"fill","value":1}},
            {"id":"OK-03","kind":"read","device":"X0","unit":"bit","count":16}
        )JSON",
                                  fx);
        QVERIFY2(ok.planError.isEmpty() && ok.resolved.ok(), qPrintable(ok.planError));
        QVERIFY2(!ok.gate.refused(), qPrintable(refusalText(ok.gate)));

        const Verdict bad = gateOf(R"JSON(
            {"id":"W-01","kind":"write","device":"Y40","unit":"bit","count":1,
                "values":{"gen":"fill","value":1}},
            {"id":"W-02","kind":"write","device":"Y@s","unit":"bit","count":17,
                "values":{"gen":"fill","value":1}},
            {"id":"W-03","kind":"write","device":"Y17","unit":"bit","count":1,
                "values":{"gen":"fill","value":1}}
        )JSON",
                                   fx);
        QVERIFY2(bad.planError.isEmpty() && bad.resolved.ok(), qPrintable(bad.planError));
        QCOMPARE(stepsOf(bad.gate), (QStringList{"W-01", "W-02", "W-03"}));
        // Y40 octal is index 32, one past Y37; the range and the scratch list are in octal.
        QCOMPARE(bad.gate.violations[0].range, QStringLiteral("Y40-Y40 (1 point)"));
        QVERIFY2(bad.gate.violations[0].why.contains(QStringLiteral("Y20-Y37")),
                 qPrintable(bad.gate.violations[0].why));
        // 17 points from Y20 run to Y40.
        QCOMPARE(bad.gate.violations[1].range, QStringLiteral("Y20-Y40 (17 points)"));
        QVERIFY2(bad.gate.violations[1].why.contains(QStringLiteral("runs past the end")),
                 qPrintable(bad.gate.violations[1].why));
        // Y17 octal is index 15, one below the scratch start.
        QCOMPARE(bad.gate.violations[2].range, QStringLiteral("Y17-Y17 (1 point)"));
    }

    void HIL_02_aWriteOutsideScratchIsRefused() {
        // The Q example with Y20-Y2F as its Y scratch, so that Y0 lies outside it.
        const Profile q =
            withScratch({"D100-D2099", "W100-W1FF", "M100-M2099", "B100-B1FF", "Y20-Y2F"});
        const Verdict v = gateOf(R"JSON(
            {"id":"W-01","kind":"write","device":"D3000","values":[1]},
            {"id":"W-02","kind":"write","device":"D@end","values":[1]},
            {"id":"W-03","kind":"write","device":"Y0","unit":"bit","values":"1"},
            {"id":"W-04","kind":"write","device":"ZR0","values":[1]},
            {"id":"W-05","kind":"write","device":"M@s16","unit":"word","count":1000,
                "values":{"gen":"fill","value":0}}
        )JSON",
                                 q);
        QCOMPARE(stepsOf(v.gate), (QStringList{"W-01", "W-02", "W-03", "W-04", "W-05"}));
        QCOMPARE(v.gate.violations[0].kind, QStringLiteral("write"));
        QCOMPARE(v.gate.violations[0].range, QStringLiteral("D3000-D3000 (1 point)"));
        QVERIFY(v.gate.violations[0].why.contains(QStringLiteral("outside the scratch area of D")));
        QVERIFY(v.gate.violations[3].why.contains(QStringLiteral("no scratch range of ZR")));
        // 1000 words of M cover 16000 points: they start in M100-M2099 and run past its end.
        QVERIFY(v.gate.violations[4].why.contains(QStringLiteral("runs past the end")));
    }

    void HIL_02_aWriteStraddlingTwoAdjacentRangesIsRefused() {
        const Profile adjacent = withScratch({"D100-D199", "D200-D299"});
        const Verdict v = gateOf(R"JSON(
            {"id":"S-01","kind":"write","device":"D190","count":20,"values":{"gen":"fill",
                "value":1}},
            {"id":"S-02","kind":"write","device":"D190","count":10,"values":{"gen":"fill",
                "value":1}},
            {"id":"S-03","kind":"write","device":"D200","count":100,"values":{"gen":"fill",
                "value":1}}
        )JSON",
                                 adjacent);
        QCOMPARE(stepsOf(v.gate), (QStringList{"S-01"})); // 190..209 straddles the two ranges
        QVERIFY2(v.gate.violations[0].why.contains(QStringLiteral("ONE scratch range")),
                 qPrintable(v.gate.violations[0].why));
        QVERIFY(v.gate.violations[0].why.contains(QStringLiteral("D100-D199")));
    }

    void HIL_02_aPollIsRefusedForSubscriptionAndWrites() {
        const Verdict v = gateOf(R"JSON(
            {"id":"P-01","kind":"poll","rounds":1,"subscribe":[{"name":"far","device":"D3000",
                "count":4}]},
            {"id":"P-02","kind":"poll","rounds":2,"subscribe":[{"name":"d","device":"D@s",
                "count":4}],
             "actions":[{"after":1,"write":{"device":"D3000","values":[1]}}]},
            {"id":"P-03","kind":"poll","rounds":1,"heartbeat":"M5000","subscribe":[{"name":"d",
                "device":"D@s","count":4}]},
            {"id":"P-04","kind":"poll","rounds":2,"subscribe":[{"name":"d","device":"D@s",
                "count":4}],
             "actions":[{"after":1,"subscribe":{"name":"far","device":"M5000","count":16}}]},
            {"id":"P-05","kind":"poll","rounds":2,"heartbeat":"M@s+200","subscribe":[{"name":"d",
                "device":"D@s","count":4},
             {"name":"x","device":"X0","count":32,"input":true}],
             "actions":[{"after":1,"write":{"device":"D@s+1","values":[7]}},
                        {"after":2,"subscribe":{"name":"in2","device":"TN0","count":4,
                            "input":true}}]}
        )JSON",
                                 qProfile());
        QCOMPARE(stepsOf(v.gate), (QStringList{"P-01", "P-02", "P-03", "P-04"})); // P-05 passes
        QVERIFY(v.gate.violations[0].kind.contains(QStringLiteral("poll subscription 'far'")));
        QVERIFY(v.gate.violations[0].why.contains(QStringLiteral("\"input\": true")));
        QCOMPARE(v.gate.violations[1].kind, QStringLiteral("poll write"));
        QCOMPARE(v.gate.violations[2].kind, QStringLiteral("poll heartbeat"));
        QVERIFY(v.gate.violations[3].kind.contains(QStringLiteral("poll subscription 'far'")));
    }

    void HIL_02_aMutateThatMovesAWriteOutOfScratchIsRefused() {
        // A write of D100 whose head device number byte 1 is changed: D100 + 0x2000 = D8292.
        const Verdict v = gateOf(R"JSON(
            {"id":"M-01","kind":"mutate","request":{"kind":"write","device":"D@s","values":[1]},
             "edits":[{"setByte":{"at":16,"value":"0x20"}}]},
            {"id":"M-02","kind":"mutate","request":{"kind":"write","device":"D@s","values":[1]},
             "edits":[{"setByte":{"at":16,"value":"0x00"}}]}
        )JSON",
                                 qProfile());
        QVERIFY2(v.resolved.ok(),
                 qPrintable(v.resolved.errors.isEmpty() ? QString() : v.resolved.errors[0].text()));
        QCOMPARE(stepsOf(v.gate), (QStringList{"M-01"}));
        QCOMPARE(v.gate.violations[0].kind, QStringLiteral("mutate (decoded as a write)"));
        QVERIFY2(v.gate.violations[0].range.startsWith(QStringLiteral("D8292-")),
                 qPrintable(v.gate.violations[0].range));
        QVERIFY(v.gate.readOnlyFrames.isEmpty());
    }

    void HIL_02_aRawWriteOutsideScratchIsRefused() {
        const McProtocol codec(qProfile().device.frame);
        const ByteBuf bytes = wordBytes({1});
        const ByteBuf outside =
            codec
                .encode(Request::writeWords(Device{DeviceType::D, 3000},
                                            ByteView{bytes.data(), bytes.size()}))
                .value();
        const ByteBuf inside =
            codec
                .encode(Request::writeWords(Device{DeviceType::D, 150},
                                            ByteView{bytes.data(), bytes.size()}))
                .value();
        const QByteArray steps = rawStepJson(QStringLiteral("R-01"), hexOf(outside)) + ',' +
                                 rawStepJson(QStringLiteral("R-02"), hexOf(inside)) + ',' +
                                 rawStepJson(QStringLiteral("R-03"), QStringLiteral("DE AD BE EF"));
        const Verdict v = gateOf(steps, qProfile());
        QCOMPARE(stepsOf(v.gate), (QStringList{"R-01", "R-03"}));
        QCOMPARE(v.gate.violations[0].kind, QStringLiteral("raw (decoded as a write)"));
        QVERIFY(v.gate.violations[1].why.contains(QStringLiteral("cannot decode")));
        QVERIFY(v.gate.violations[1].why.contains(QStringLiteral("readOnly")));
    }

    void HIL_02_undecodableFramesNeedReadOnlyAndARecoverAndAreListed() {
        const Verdict v = gateOf(R"JSON(
            {"id":"U-01","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"truncate":1}],"readOnly":true,"recover":"reconnect"},
            {"id":"U-02","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"truncate":1}]},
            {"id":"U-03","kind":"raw","hex":"DE AD BE EF","readOnly":true,"recover":"reconnect"},
            {"id":"U-04","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"setByte":{"at":6,"value":"0x00"}}],"readOnly":true},
            {"id":"U-05","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"setByte":{"at":11,"value":"0x03"}}]},
            {"id":"U-06","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"setByte":{"at":11,"value":"0x03"}}],"readOnly":true,
                "recover":"reconnect"},
            {"id":"U-07","kind":"mutate","request":{"kind":"write","device":"D@s","values":[1]},
                "edits":[{"append":"30"}]},
            {"id":"U-08","kind":"mutate","request":{"kind":"write","device":"D3000","values":[1]},
                "edits":[{"append":"30"}],"readOnly":true,"recover":"reconnect"},
            {"id":"U-09","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"truncate":1}],"readOnly":true}
        )JSON",
                                 qProfile());
        // U-02, U-05 and U-07 are not fully decodable and not declared. U-05 is the 0403 command
        // the mock frames but does not execute: its log record looks like a read of D0 x 0, and
        // must not pass as one. U-08 is declared, yet its base request is a write (refused as such)
        // its command 1401 is no read-only command, and the part the mock understood is a write
        // outside scratch. U-09 is declared but names no recovery.
        QCOMPARE(stepsOf(v.gate),
                 (QStringList{"U-02", "U-05", "U-07", "U-08", "U-08", "U-08", "U-09"}));
        QCOMPARE(kindsOf(v.gate, QStringLiteral("U-08")),
                 (QStringList{"mutate (declared readOnly)", "mutate (declared readOnly)",
                              "mutate (decoded as a write)"}));
        QVERIFY(allowListWhy(v.gate, QStringLiteral("U-08")).contains(QStringLiteral("1401")));
        QVERIFY(v.gate.violations[2].why.contains(QStringLiteral("cannot decode part of")));
        QVERIFY2(v.gate.violations[6].why.contains(QStringLiteral("\"recover\": \"reconnect\"")),
                 qPrintable(v.gate.violations[6].why));
        // U-01, U-03 and U-06 are declared, not fully decodable and recover: listed for the prompt
        // (U-08 too: its recover is right, its other faults are refused on their own). U-04 is
        // declared but the mock decodes it completely, so it needs neither a mention nor a
        // recovery.
        QStringList listed;
        for (const UndecodedFrame& f : v.gate.readOnlyFrames) {
            listed << f.stepId;
        }
        QCOMPARE(listed, (QStringList{"U-01", "U-03", "U-06", "U-08"}));
        QCOMPARE(v.gate.readOnlyFrames[1].hex, QStringLiteral("DE AD BE EF"));
    }

    // ---- HIL-02: readOnly is not a way past the gate ------------------------------------------
    //
    // The three attacks of the Phase 7 tester. Each one hands the PLC a write that no single frame
    // shows: a truncated write whose missing bytes arrive with the next frame, a fragment that
    // completes the one before, and a length field that swallows a second frame.

    void HIL_02_aTruncatedWriteCompletedByTheNextFrameIsRefused() {
        // Control: the two byte strings really are one write of D50 = 7 when they meet.
        const Profile q = qProfile();
        const McProtocol codec(q.device.frame);
        const ByteBuf one = wordBytes({1});
        ByteBuf head = codec
                           .encode(Request::writeWords(Device{DeviceType::D, 100},
                                                       ByteView{one.data(), one.size()}))
                           .value();
        head.resize(head.size() - 8); // the device, the count and the data are gone
        const ByteBuf tail = {0x32, 0x00, 0x00, 0xA8, 0x01, 0x00, 0x07, 0x00}; // D50, 1 word, 7
        MockPlc control(q.device.frame);
        ByteBuf joined = head;
        joined.insert(joined.end(), tail.begin(), tail.end());
        control.bytesIn(ByteView{joined.data(), joined.size()});
        QCOMPARE(control.word(Device{DeviceType::D, 50}), uint16_t(7));

        for (const char* recover :
             {"", ",\"recover\":\"none\"", ",\"recover\":\"eot\"", ",\"recover\":\"reconnect\""}) {
            const QString extra = QString::fromLatin1(recover);
            const QByteArray steps = QStringLiteral(R"JSON(
                {"id":"X-01","kind":"mutate","request":{"kind":"write","device":"D@s","values":[1]},
                 "edits":[{"truncate":8}],"readOnly":true%1},
                {"id":"X-02","kind":"raw","hex":"32 00 00 A8 01 00 07 00","readOnly":true%1},
                {"id":"X-03","kind":"read","device":"D50"})JSON")
                                         .arg(extra)
                                         .toUtf8();
            const Verdict v = gateOf(steps, q);
            QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
            QVERIFY2(v.gate.refused(), recover);
            // X-01 is refused whatever its recovery says: the base request is a write.
            QVERIFY2(kindsOf(v.gate, QStringLiteral("X-01"))
                         .contains(QStringLiteral("mutate (declared readOnly)")),
                     recover);
            // X-02 passes only with the recovery that throws the fragment away before anything
            // else.
            const bool right = extra.contains(QStringLiteral("reconnect"));
            QCOMPARE(kindsOf(v.gate, QStringLiteral("X-02")).isEmpty(), right);
            QVERIFY(kindsOf(v.gate, QStringLiteral("X-03")).isEmpty());
        }
    }

    void HIL_02_aRefusedAttackSendsNothingEvenWithYesAndTheRightId() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QString dir = scratchDir(QStringLiteral("gate_attack"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(planWith(R"JSON(
            {"id":"X-01","kind":"mutate","request":{"kind":"write","device":"D@s","values":[1]},
             "edits":[{"truncate":8}],"readOnly":true,"recover":"reconnect"},
            {"id":"X-02","kind":"raw","hex":"32 00 00 A8 01 00 07 00","readOnly":true,
                "recover":"reconnect"},
            {"id":"X-03","kind":"read","device":"D50"})JSON"));
        planFile.close();
        Options options;
        options.profilePath = writeLoopbackProfile(dir, server.serverPort());
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/out");
        options.yes = true;
        const ToolRun run = runToolWith(options, QStringLiteral("q03ude-eth-3e-bin\n"));
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::GateRefused));
        QVERIFY(run.err.contains(QStringLiteral("X-01")));
        QVERIFY(
            run.err.contains(QStringLiteral("a mutate of a write can never be declared readOnly")));
        QVERIFY(!run.err.contains(QStringLiteral("X-02")));
        QVERIFY(!server.waitForNewConnection(300));
        QVERIFY(!QDir(dir + QStringLiteral("/out")).exists());
    }

    void HIL_02_aRawFragmentNeedsTheRecoveryOfItsTransport() {
        const auto withRecover = [&](const char* recover) {
            QJsonObject o =
                QJsonDocument::fromJson(
                    rawStepJson(QStringLiteral("F-01"), QStringLiteral("32 00 00 A8 01 00 07 00")))
                    .object();
            o.insert(QStringLiteral("readOnly"), true);
            if (recover[0] != '\0') {
                o.insert(QStringLiteral("recover"), QLatin1String(recover));
            }
            return QJsonDocument(o).toJson(QJsonDocument::Compact);
        };
        // Ethernet: a reconnect throws the fragment away; an EOT means nothing to an Ethernet
        // module.
        const Profile q = qProfile();
        QVERIFY(gateOf(withRecover(""), q).gate.refused());
        QVERIFY(gateOf(withRecover("none"), q).gate.refused());
        QVERIFY(gateOf(withRecover("eot"), q).gate.refused());
        const Verdict ok = gateOf(withRecover("reconnect"), q);
        QVERIFY2(!ok.gate.refused(), qPrintable(refusalText(ok.gate)));
        QCOMPARE(ok.gate.readOnlyFrames.size(), 1);
        // A serial line: only an EOT clears a half-received frame.
        const Profile c24 = loadExample(QStringLiteral("q03ude-c24-3c-f4"));
        QVERIFY(gateOf(withRecover(""), c24).gate.refused());
        QVERIFY(gateOf(withRecover("reconnect"), c24).gate.refused());
        const Verdict serialOk = gateOf(withRecover("eot"), c24);
        QVERIFY2(!serialOk.gate.refused(), qPrintable(refusalText(serialOk.gate)));
    }

    void HIL_02_aFrameLeftHalfReceivedIsNotUnderstood() {
        // A complete read followed by the start of another frame: the mock decodes the read and
        // keeps waiting for the rest of the next one. That is a fragment left on the line, readOnly
        // or not.
        const Verdict v = gateOf(R"JSON(
            {"id":"H-01","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"append":"50 00 00 FF FF"}]},
            {"id":"H-02","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"append":"50 00 00 FF FF"}],"readOnly":true},
            {"id":"H-03","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"append":"50 00 00 FF FF"}],"readOnly":true,"recover":"reconnect"},
            {"id":"H-04","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"setByte":{"at":6,"value":"0x00"}}]}
        )JSON",
                                 qProfile());
        QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
        QCOMPARE(stepsOf(v.gate), (QStringList{"H-01", "H-02"}));
        QVERIFY(v.gate.violations[0].why.contains(QStringLiteral("cannot decode")));
        QCOMPARE(v.gate.readOnlyFrames.size(), 1); // H-03; H-04 is decoded completely
    }

    void HIL_02_aLengthFieldThatSwallowsASecondFrameIsRefused() {
        const Profile q = qProfile();
        const McProtocol codec(q.device.frame);
        const ByteBuf one = wordBytes({7});
        const ByteBuf read =
            codec.encode(Request::readWords(Device{DeviceType::D, 100}, 1)).value();
        const auto swallowing = [&](uint32_t writeAt) {
            const ByteBuf write = codec
                                      .encode(Request::writeWords(Device{DeviceType::D, writeAt},
                                                                  ByteView{one.data(), one.size()}))
                                      .value();
            ByteBuf frame = read;
            frame.insert(frame.end(), write.begin(), write.end());
            // 3E Binary: the request data length (two bytes, little endian) sits at offset 7 and
            // counts every byte after it; here it also counts the whole second frame.
            const size_t length = frame.size() - 9;
            frame[7] = static_cast<uint8_t>(length & 0xFF);
            frame[8] = static_cast<uint8_t>(length >> 8);
            return hexOf(frame);
        };
        // D50 is outside scratch (D100-D2099), D105 inside.
        for (const char* recover : {"", ",\"recover\":\"reconnect\""}) {
            const QByteArray steps =
                QStringLiteral(R"JSON(
                {"id":"R-10","kind":"raw","hex":"%1","readOnly":true%3},
                {"id":"R-11","kind":"raw","hex":"%2","readOnly":true%3})JSON")
                    .arg(swallowing(50), swallowing(105), QString::fromLatin1(recover))
                    .toUtf8();
            const Verdict v = gateOf(steps, q);
            QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
            // The read is 21 bytes long: the swallowed write starts at byte 21.
            QVERIFY2(kindsOf(v.gate, QStringLiteral("R-10"))
                         .contains(QStringLiteral("raw (write embedded at byte 21)")),
                     qPrintable(kindsOf(v.gate, QStringLiteral("R-10")).join(QLatin1Char('|'))));
            // A write inside scratch behind a readOnly read is refused too: it is a write, and the
            // frame was called read-only. The allow-list names it at the same offset.
            QVERIFY2(allowListWhy(v.gate, QStringLiteral("R-11"))
                         .startsWith(QStringLiteral("command 1401 at byte 21|")),
                     qPrintable(allowListWhy(v.gate, QStringLiteral("R-11"))));
        }
    }

    // ---- HIL-02: a readOnly frame carries only a read command -----------------------------------
    //
    // The gate finds the command field itself. A command outside the family's read-only list is
    // refused, whether or not the mock can decode it; a frame that shows no command at all is a
    // fragment and meets the recovery rule instead.

    void HIL_02_aReadOnlyFrameCarriesOnlyAReadCommand_data() {
        QTest::addColumn<QString>("profile");
        QTest::addColumn<QString>("hex");
        QTest::addColumn<QString>("refused"); // "<code>@<byte>" the gate names; empty: admitted

        const auto row = [](const QString& name, const QString& profile, const QByteArray& bytes,
                            const QString& refused) {
            QTest::newRow(qPrintable(name)) << profile << spaced(bytes) << refused;
        };
        // 3E and 4E, Binary: 0401 0403 0406 read, every other command is refused.
        for (const QString& command :
             {QStringLiteral("0401"), QStringLiteral("0403"), QStringLiteral("0406")}) {
            row(QStringLiteral("3E bin %1 admitted").arg(command), QStringLiteral("3E-bin"),
                frame3EBin(qnaBodyBin(command)), QString());
            row(QStringLiteral("4E bin %1 admitted").arg(command), QStringLiteral("3E-bin"),
                frame4EBin(qnaBodyBin(command)), QString());
        }
        for (const QString& command :
             {QStringLiteral("0801"), QStringLiteral("0619"), QStringLiteral("1401"),
              QStringLiteral("1402"), QStringLiteral("1406")}) {
            row(QStringLiteral("3E bin %1 refused").arg(command), QStringLiteral("3E-bin"),
                frame3EBin(qnaBodyBin(command)), command + QStringLiteral("@0"));
            row(QStringLiteral("4E bin %1 refused").arg(command), QStringLiteral("3E-bin"),
                frame4EBin(qnaBodyBin(command)), command + QStringLiteral("@0"));
        }
        // 3E and 4E, ASCII.
        for (const bool fourE : {false, true}) {
            const QString name = fourE ? QStringLiteral("4E ascii") : QStringLiteral("3E ascii");
            for (const QString& command : {QStringLiteral("0401"), QStringLiteral("0403")}) {
                row(name + QStringLiteral(" %1 admitted").arg(command), QStringLiteral("3E-ascii"),
                    frameQnaAscii(fourE, qnaBodyAscii(command)), QString());
            }
            for (const QString& command :
                 {QStringLiteral("1401"), QStringLiteral("1402"), QStringLiteral("0801")}) {
                row(name + QStringLiteral(" %1 refused").arg(command), QStringLiteral("3E-ascii"),
                    frameQnaAscii(fourE, qnaBodyAscii(command)), command + QStringLiteral("@0"));
            }
        }
        // No command to read: the bytes are a fragment (the recovery rule takes it).
        row(QStringLiteral("3E bin command cut off"), QStringLiteral("3E-bin"),
            frame3EBin(qnaBodyBin(QStringLiteral("1402"))).left(12), QString());
        row(QStringLiteral("3E bin header only"), QStringLiteral("3E-bin"),
            frame3EBin(qnaBodyBin(QStringLiteral("1402"))).left(11), QString());
        {
            QByteArray wrongSubheader = frame3EBin(qnaBodyBin(QStringLiteral("1402")));
            wrongSubheader[0] = 0x51; // not a 3E frame: the PLC drops it (plan step G7-02)
            row(QStringLiteral("3E bin subheader 51H"), QStringLiteral("3E-bin"), wrongSubheader,
                QString());
        }
        // A second frame that follows the first, or that hides behind a swallowing length.
        row(QStringLiteral("3E bin read then read"), QStringLiteral("3E-bin"),
            frame3EBin(qnaBodyBin(QStringLiteral("0401"))) +
                frame3EBin(qnaBodyBin(QStringLiteral("0403"))),
            QString());
        row(QStringLiteral("3E bin read then 1402"), QStringLiteral("3E-bin"),
            frame3EBin(qnaBodyBin(QStringLiteral("0401"))) +
                frame3EBin(qnaBodyBin(QStringLiteral("1402"))),
            QStringLiteral("1402@21"));
        {
            QByteArray swallowing = frame3EBin(qnaBodyBin(QStringLiteral("0401"))) +
                                    frame3EBin(qnaBodyBin(QStringLiteral("1402")));
            const int length = static_cast<int>(swallowing.size()) - 9;
            swallowing[7] = static_cast<char>(length & 0xFF);
            swallowing[8] = static_cast<char>(length >> 8);
            row(QStringLiteral("3E bin length swallows 1402"), QStringLiteral("3E-bin"), swallowing,
                QStringLiteral("1402@21"));
        }
        row(QStringLiteral("junk then 1402"), QStringLiteral("3E-bin"),
            QByteArray::fromHex("DEADBEEF") + frame3EBin(qnaBodyBin(QStringLiteral("1402"))),
            QStringLiteral("1402@4"));

        // 1E, Binary and ASCII: 00H and 01H read. 02H-05H write.
        for (const bool text : {false, true}) {
            const QString name = text ? QStringLiteral("1E ascii") : QStringLiteral("1E bin");
            const QString profile = text ? QStringLiteral("1E-ascii") : QStringLiteral("1E-bin");
            const auto frame = [&](unsigned command, bool write) {
                return text ? frame1EAscii(command, write) : frame1EBin(command, write);
            };
            const auto hexCode = [](unsigned command) {
                return QStringLiteral("%1").arg(command, 2, 16, QLatin1Char('0')).toUpper();
            };
            const size_t readSize = text ? 24 : 12;
            row(name + QStringLiteral(" 00H admitted"), profile, frame(0x00, false), QString());
            row(name + QStringLiteral(" 01H admitted"), profile, frame(0x01, false), QString());
            row(name + QStringLiteral(" 01H then 00H admitted"), profile,
                frame(0x01, false) + frame(0x00, false), QString());
            for (const unsigned command : {0x02u, 0x03u, 0x04u, 0x05u, 0x17u, 0x3Cu}) {
                row(name + QStringLiteral(" %1H refused").arg(hexCode(command)), profile,
                    frame(command, true), hexCode(command) + QStringLiteral("@0"));
            }
            row(name + QStringLiteral(" 01H then 05H refused"), profile,
                frame(0x01, false) + frame(0x05, true), QStringLiteral("05@%1").arg(readSize));
            // A first byte above 3CH is no 1E command: a malformed frame (plan step G7-02), a
            // fragment while it is no longer than one read request. Longer, it could hide a write
            // that no locator finds (1E has no start mark), and it is refused.
            row(name + QStringLiteral(" 7FH is no command"), profile, frame(0x7F, false),
                QString());
            row(name + QStringLiteral(" 3DH is no command"), profile, frame(0x3D, false),
                QString());
            // The limit is exact: one read request is admitted, one byte more is refused.
            row(name + QStringLiteral(" 7FH of exactly one read request admitted"), profile,
                frame(0x7F, false), QString());
            row(name + QStringLiteral(" 7FH one byte over a read request is refused"), profile,
                frame(0x7F, false) + (text ? ascii("0") : QByteArray(1, '\0')),
                QStringLiteral("7F@0"));
            // A short frame that starts with a command code of the range 06H-3CH is refused: only
            // a first byte above 3CH is "no command".
            for (const unsigned code : {0x06u, 0x3Au, 0x3Cu}) {
                row(name + QStringLiteral(" short frame of %1H is refused").arg(hexCode(code)),
                    profile, frame(code, false), hexCode(code) + QStringLiteral("@0"));
            }
            row(name + QStringLiteral(" 3DH longer than a read is refused"), profile,
                frame(0x3D, true), QStringLiteral("3D@0"));
            row(name + QStringLiteral(" 7FH then a 05H write is refused"), profile,
                (text ? ascii("7F") : QByteArray(1, '\x7F')) + frame(0x05, true),
                QStringLiteral("7F@0"));
        }

        // 3C, formats 1-4: the same QnA commands.
        for (int format = 1; format <= 4; ++format) {
            const QString key = QStringLiteral("3C-F%1").arg(format);
            for (const QString& command : {QStringLiteral("0401"), QStringLiteral("0403")}) {
                row(key + QLatin1Char(' ') + command + QStringLiteral(" admitted"), key,
                    serialFrame(format, false, command.toLatin1() + ascii("0000D*0000640001")),
                    QString());
            }
            for (const QString& command : {QStringLiteral("1401"), QStringLiteral("1402")}) {
                row(key + QLatin1Char(' ') + command + QStringLiteral(" refused"), key,
                    serialFrame(format, false, command.toLatin1() + ascii("0000D*0000640001")),
                    command + QStringLiteral("@0"));
            }
        }

        // 1C, formats 1-4, ACPU and AnA/AnU letters: BR and WR (JR and QR) read.
        for (int format = 1; format <= 4; ++format) {
            const QString key = QStringLiteral("1C-F%1").arg(format);
            for (const char* command : {"BR", "WR"}) {
                row(key + QLatin1Char(' ') + QLatin1String(command) + QStringLiteral(" admitted"),
                    key,
                    serialFrame(format, true,
                                ascii(command) + ascii("0D0000"
                                                       "01")),
                    QString());
            }
            for (const char* command : {"BW", "WW", "BT", "WT", "ZZ"}) {
                row(key + QLatin1Char(' ') + QLatin1String(command) + QStringLiteral(" refused"),
                    key,
                    serialFrame(format, true,
                                ascii(command) + ascii("0D0000"
                                                       "01")),
                    QLatin1String(command) + QStringLiteral("@0"));
            }
        }
        for (const char* command : {"JR", "QR"}) {
            row(QStringLiteral("1C AnA %1 admitted").arg(QLatin1String(command)),
                QStringLiteral("1C-ana"),
                serialFrame(1, true,
                            ascii(command) + ascii("0D0000"
                                                   "01")),
                QString());
        }
        for (const char* command : {"JW", "QW", "JT", "QT"}) {
            row(QStringLiteral("1C AnA %1 refused").arg(QLatin1String(command)),
                QStringLiteral("1C-ana"),
                serialFrame(1, true,
                            ascii(command) + ascii("0D0000"
                                                   "01")),
                QLatin1String(command) + QStringLiteral("@0"));
        }

        // 1C command letters in lower case are not the listed read letters: refused.
        for (const char* command : {"br", "wr", "bt", "bw"}) {
            row(QStringLiteral("1C F1 %1 in lower case is refused").arg(QLatin1String(command)),
                QStringLiteral("1C-F1"), serialFrame(1, true, ascii(command) + ascii("3D010001")),
                QLatin1String(command) + QStringLiteral("@0"));
        }

        // A frame cut off right after its command still names the command: refused. Cut one
        // byte earlier it shows no command and is a fragment (the rows above).
        row(QStringLiteral("3E bin 1402 cut right after its command"), QStringLiteral("3E-bin"),
            frame3EBin(qnaBodyBin(QStringLiteral("1402"))).left(13), QStringLiteral("1402@0"));
        row(QStringLiteral("3E bin 1401 cut right after its command"), QStringLiteral("3E-bin"),
            frame3EBin(qnaBodyBin(QStringLiteral("1401"))).left(13), QStringLiteral("1401@0"));
        row(QStringLiteral("4E bin 1402 cut right after its command"), QStringLiteral("3E-bin"),
            frame4EBin(qnaBodyBin(QStringLiteral("1402"))).left(17), QStringLiteral("1402@0"));
        row(QStringLiteral("3E ascii 1401 cut right after its command"), QStringLiteral("3E-ascii"),
            frameQnaAscii(false, qnaBodyAscii(QStringLiteral("1401"))).left(26),
            QStringLiteral("1401@0"));
        row(QStringLiteral("3E bin 0401 cut right after its command is a fragment"),
            QStringLiteral("3E-bin"), frame3EBin(qnaBodyBin(QStringLiteral("0401"))).left(13),
            QString());
        row(QStringLiteral("1E bin 05H cut right after its command"), QStringLiteral("1E-bin"),
            frame1EBin(0x05, true).left(1), QStringLiteral("05@0"));
        row(QStringLiteral("1E ascii 05H cut right after its command"), QStringLiteral("1E-ascii"),
            frame1EAscii(0x05, true).left(2), QStringLiteral("05@0"));
        row(QStringLiteral("3C F4 1402 cut right after its command"), QStringLiteral("3C-F4"),
            serialFrame(4, false, ascii("1402")).left(15), QStringLiteral("1402@0"));
        row(QStringLiteral("1C F1 BT cut right after its command"), QStringLiteral("1C-F1"),
            serialFrame(1, true, ascii("BT")).left(7), QStringLiteral("BT@0"));

        // Another family on the same port: the gate looks for every frame a port could take.
        // Ethernet: a 3E/4E frame of the other data code, a 3E frame on a 1E profile.
        row(QStringLiteral("3E ascii frame on a binary profile"), QStringLiteral("3E-bin"),
            frameQnaAscii(false, qnaBodyAscii(QStringLiteral("1401"))), QStringLiteral("1401@0"));
        row(QStringLiteral("3E bin frame on an ascii profile"), QStringLiteral("3E-ascii"),
            frame3EBin(qnaBodyBin(QStringLiteral("1402"))), QStringLiteral("1402@0"));
        row(QStringLiteral("3E bin frame on a 1E profile"), QStringLiteral("1E-bin"),
            frame3EBin(qnaBodyBin(QStringLiteral("1402"))), QStringLiteral("1402@0"));
        // A 3E read is a read, but on a 1E profile its first byte is no 1E command and it is
        // longer than one 1E read request.
        row(QStringLiteral("3E bin read on a 1E profile"), QStringLiteral("1E-bin"),
            frame3EBin(qnaBodyBin(QStringLiteral("0401"))), QStringLiteral("50@0"));
        // A frame of another format on the port (format 2 carries a block number): its id is
        // found with and without one.
        row(QStringLiteral("3C format 2 frame on a format 1 port"), QStringLiteral("3C-F1"),
            serialFrame(2, false, ascii("14010000D*0000500001") + ascii("0007")),
            QStringLiteral("1401@0"));
        row(QStringLiteral("3C format 1 frame on a format 2 port"), QStringLiteral("3C-F2"),
            serialFrame(1, false, ascii("14010000D*0000500001") + ascii("0007")),
            QStringLiteral("1401@0"));
        // Serial: a 4C frame (F8) or a 1C frame on a 3C port, a 3C or 4C frame on a 1C port,
        // in every format.
        for (int format = 1; format <= 4; ++format) {
            const QString f3 = QStringLiteral("3C-F%1").arg(format);
            const QString f1 = QStringLiteral("1C-F%1").arg(format);
            row(QStringLiteral("4C 1401 on ") + f3, f3,
                frame4C(format, ascii("14010000D*0000500001") + ascii("0007")),
                QStringLiteral("1401@0"));
            row(QStringLiteral("4C 0401 on ") + f3 + QStringLiteral(" admitted"), f3,
                frame4C(format, ascii("04010000D*0000500001")), QString());
            row(QStringLiteral("1C BT on ") + f3, f3,
                serialFrame(format, true, ascii("BT3D0050010007")), QStringLiteral("BT@0"));
            row(QStringLiteral("1C BR on ") + f3 + QStringLiteral(" admitted"), f3,
                serialFrame(format, true, ascii("BR3D005001")), QString());
            row(QStringLiteral("3C 1401 on ") + f1, f1,
                serialFrame(format, false, ascii("14010000D*0000500001") + ascii("0007")),
                QStringLiteral("1401@0"));
            row(QStringLiteral("4C 1402 on ") + f1, f1,
                frame4C(format, ascii("14020000D*0000500001") + ascii("0007")),
                QStringLiteral("1402@0"));
            row(QStringLiteral("3C 0401 on ") + f1 + QStringLiteral(" admitted"), f1,
                serialFrame(format, false, ascii("04010000D*0000500001")), QString());
        }
    }

    void HIL_02_aReadOnlyFrameCarriesOnlyAReadCommand() {
        QFETCH(QString, profile);
        QFETCH(QString, hex);
        QFETCH(QString, refused);
        const Profile p = profileForRow(profile);
        QVERIFY2(!p.id.isEmpty(), qPrintable(profile));
        const char* recover = p.device.frame.isSerial() ? "eot" : "reconnect";
        const Verdict v = gateOf(readOnlyRawJson(QStringLiteral("A-01"), hex, recover), p);
        QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
        const QString why = allowListWhy(v.gate, QStringLiteral("A-01"));
        if (refused.isEmpty()) {
            QVERIFY2(!v.gate.refused(), qPrintable(refusalText(v.gate)));
            QVERIFY2(why.isEmpty(), qPrintable(why));
            return;
        }
        QVERIFY2(v.gate.refused(), "an allow-list command was admitted");
        const QString code = refused.section(QLatin1Char('@'), 0, 0);
        const QString at = refused.section(QLatin1Char('@'), 1, 1);
        QVERIFY2(why.startsWith(QStringLiteral("command %1 at byte %2|").arg(code, at)),
                 qPrintable(why + QStringLiteral(" / ") + refusalText(v.gate)));
        QVERIFY2(why.contains(QStringLiteral("is not a read-only command")), qPrintable(why));
        // The refusal names the step, and the same frame without readOnly is refused too.
        QVERIFY(refusalText(v.gate).contains(QStringLiteral("A-01")));
    }

    void HIL_02_theTestersTwoGF1bReprosAreRefusedByTheAllowListAlone() {
        // A COMPLETE write the mock cannot decode, declared readOnly with the right recovery.
        // Nothing but the allow-list stops it.
        const Profile q = qProfile();
        const QString random1402 =
            QStringLiteral("50 00 00 FF FF 03 00 0E 00 10 00 02 14 00 00 01 00 32 00 00 A8 07 00");
        // The mock does not decode it: it logs no write, and D50 stays 0 (so the gate's mock
        // alone could not have refused it).
        MockPlc control(q.device.frame);
        const QByteArray bytes = QByteArray::fromHex(random1402.toLatin1());
        control.bytesIn(ByteView{reinterpret_cast<const uint8_t*>(bytes.constData()),
                                 static_cast<size_t>(bytes.size())});
        QCOMPARE(control.word(Device{DeviceType::D, 50}), uint16_t(0));

        const QByteArray steps = readOnlyRawJson(QStringLiteral("X-01"), random1402, "reconnect") +
                                 ',' + QByteArray(R"JSON(
            {"id":"X-02","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"setByte":{"at":11,"value":"0x02"}},{"setByte":{"at":12,"value":"0x14"}},
                    {"setByte":{"at":15,"value":"0x01"}},{"setByte":{"at":16,"value":"0x00"}},
                    {"setByte":{"at":17,"value":"0x32"}},{"setByte":{"at":18,"value":"0x00"}},
                    {"setByte":{"at":19,"value":"0x00"}},{"setByte":{"at":20,"value":"0xA8"}},
                    {"append":"07 00"},{"setByte":{"at":7,"value":"0x0E"}}],
                "readOnly":true,"recover":"reconnect"},
            {"id":"X-03","kind":"read","device":"D50"})JSON");
        const Verdict v = gateOf(steps, q);
        QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
        // X-02 really is the same frame as X-01 once edited.
        QVERIFY(v.resolved.steps[1].ops[0].frames[0] == v.resolved.steps[0].ops[0].frames[0]);
        QCOMPARE(kindsOf(v.gate, QStringLiteral("X-01")), (QStringList{"raw (declared readOnly)"}));
        QCOMPARE(kindsOf(v.gate, QStringLiteral("X-02")),
                 (QStringList{"mutate (declared readOnly)"}));
        QVERIFY(allowListWhy(v.gate, QStringLiteral("X-01")).contains(QStringLiteral("1402")));
        QVERIFY(allowListWhy(v.gate, QStringLiteral("X-02")).contains(QStringLiteral("1402")));
        QVERIFY(kindsOf(v.gate, QStringLiteral("X-03")).isEmpty());
    }

    void HIL_02_aReadOnlyWriteIsRefusedAlsoInsideScratchAndAlsoWhenTheMockDecodesIt() {
        // 1401 into scratch: the mock decodes it and the scratch check passes, but the plan called
        // the frame readOnly, and it is not.
        const Profile q = qProfile();
        const Verdict v = gateOf(readOnlyRawJson(QStringLiteral("W-01"),
                                                 spaced(frame3EBin(QByteArray::fromHex("01140000"
                                                                                       "640000A8"
                                                                                       "0100"
                                                                                       "0700"))),
                                                 "reconnect"),
                                 q);
        QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
        QCOMPARE(kindsOf(v.gate, QStringLiteral("W-01")), (QStringList{"raw (declared readOnly)"}));
        QVERIFY(allowListWhy(v.gate, QStringLiteral("W-01")).contains(QStringLiteral("1401")));
    }

    void HIL_02_aRefusedReadOnlyWriteSendsNothingEvenWithYesAndTheRightId() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QString dir = scratchDir(QStringLiteral("gate_allowlist"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(planWith(
            readOnlyRawJson(QStringLiteral("X-01"),
                            QStringLiteral("50 00 00 FF FF 03 00 0E 00 10 00 02 14 00 00 01 00 32 "
                                           "00 00 A8 07 00"),
                            "reconnect") +
            R"JSON(,{"id":"X-03","kind":"read","device":"D50"})JSON"));
        planFile.close();
        Options options;
        options.profilePath = writeLoopbackProfile(dir, server.serverPort());
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/out");
        options.yes = true;
        const ToolRun run = runToolWith(options, QStringLiteral("q03ude-eth-3e-bin\n"));
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::GateRefused));
        QVERIFY2(run.err.contains(QStringLiteral("X-01")), qPrintable(run.err));
        QVERIFY2(run.err.contains(QStringLiteral("command 1402")), qPrintable(run.err));
        QVERIFY(!server.waitForNewConnection(300));
        QVERIFY(!QDir(dir + QStringLiteral("/out")).exists());
    }

    // ---- HIL-02: default deny, readOnly or not -------------------------------------------------
    //
    // Every command in the bytes of a raw or mutate frame is a read, or a write the mock decodes
    // at that offset and that lies in scratch. The frames are written out from the reference spec.

    void HIL_02_aCommandTheMockIgnoresIsRefusedAlsoWithoutReadOnly() {
        // On a serial line the mock drops a command it has no layout
        // for without a trace, so a frame that is "understood" can hide a write behind a read, or
        // behind an EOT. None of these is declared readOnly.
        const auto frames = [](int format, bool oneC, const QString& code) {
            const QByteArray data =
                oneC ? ascii(qPrintable(code)) + ascii("3D0050010007")
                     : ascii(qPrintable(code)) + ascii("0000D*0000500001") + ascii("0007");
            return serialFrame(format, oneC, data);
        };
        const QStringList qnaWrites{"1406", "0802", "1005", "0619", "0101", "1001", "0613"};
        const QStringList oneCWrites{"BT", "WT", "JW", "QW", "JT", "QT",
                                     "ZZ", "RR", "RS", "PC", "GW", "TT"};
        for (int format = 1; format <= 4; ++format) {
            const QByteArray eot = format == 4 ? QByteArray::fromHex("040D0A") : QByteArray("\x04");
            for (const bool oneC : {false, true}) {
                const Profile p =
                    profileForRow(QStringLiteral("%1-F%2")
                                      .arg(oneC ? QStringLiteral("1C") : QStringLiteral("3C"))
                                      .arg(format));
                const QByteArray inScratch =
                    oneC
                        ? serialFrame(format, true, ascii("WW3D0100010007"))
                        : serialFrame(format, false, ascii("14010000D*0001000001") + ascii("0007"));
                const QByteArray read =
                    oneC ? serialFrame(format, true, ascii("BR3D010001"))
                         : serialFrame(format, false, ascii("04010000D*0000640001"));
                for (const QString& code : oneC ? oneCWrites : qnaWrites) {
                    const QByteArray write = frames(format, oneC, code);
                    const QString what =
                        QStringLiteral("format %1 %2 %3")
                            .arg(format)
                            .arg(oneC ? QStringLiteral("1C") : QStringLiteral("3C"))
                            .arg(code);
                    const QByteArray steps =
                        rawStepJson(QStringLiteral("S-01"), spaced(read + write)) + ',' +
                        rawStepJson(QStringLiteral("S-02"), spaced(eot + write)) + ',' +
                        rawStepJson(QStringLiteral("S-03"), spaced(write)) + ',' +
                        rawStepJson(QStringLiteral("S-04"), spaced(read + eot + write)) + ',' +
                        rawStepJson(QStringLiteral("S-05"), spaced(write + inScratch));
                    const Verdict v = gateOf(steps, p);
                    QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
                    const auto expected = [&](int offset) {
                        return QStringList{QStringLiteral("command %1 at byte %2")
                                               .arg(code.toUpper())
                                               .arg(offset)};
                    };
                    QCOMPARE(unaccounted(v.gate, QStringLiteral("S-01")),
                             expected(static_cast<int>(read.size())));
                    QCOMPARE(unaccounted(v.gate, QStringLiteral("S-02")),
                             expected(static_cast<int>(eot.size())));
                    QCOMPARE(unaccounted(v.gate, QStringLiteral("S-03")), expected(0));
                    QCOMPARE(unaccounted(v.gate, QStringLiteral("S-04")),
                             expected(static_cast<int>(read.size() + eot.size())));
                    // A decodable write in scratch behind the unknown command does not account for
                    // it: the mock skips the unknown frame and decodes the next one.
                    QCOMPARE(unaccounted(v.gate, QStringLiteral("S-05")), expected(0));
                    QVERIFY2(v.gate.refused(), qPrintable(what));
                }
            }
        }
    }

    void HIL_02_aFrameOfAnotherFamilyIsRefusedAlsoWithoutReadOnly() {
        // A 4C frame (frame id F8) or a 1C frame on a 3C port, a 3C frame on a 1C port.
        const QByteArray fourC1401 = QByteArray(1, '\x05') + ascii("F80000FF03FF0000") +
                                     ascii("14010000D*0000500001") + ascii("0007") + ascii("00") +
                                     ascii("\r\n");
        const QByteArray oneCBt =
            QByteArray(1, '\x05') + ascii("00FFBT3D0050010007") + ascii("E6\r\n");
        const QByteArray threeC1401 = QByteArray(1, '\x05') + ascii("F90000FF00") +
                                      ascii("14010000D*0000500001") + ascii("0007") + ascii("00") +
                                      ascii("\r\n");
        const Profile c24 = profileForRow(QStringLiteral("3C-F4"));
        const Profile oneC = profileForRow(QStringLiteral("1C-F4"));
        const QByteArray steps = rawStepJson(QStringLiteral("D-01"), spaced(fourC1401)) + ',' +
                                 rawStepJson(QStringLiteral("D-02"), spaced(oneCBt)) + ',' +
                                 rawStepJson(QStringLiteral("D-03"), spaced(threeC1401));
        const Verdict on3C = gateOf(steps, c24);
        QCOMPARE(unaccounted(on3C.gate, QStringLiteral("D-01")),
                 (QStringList{"command 1401 at byte 0"}));
        QCOMPARE(unaccounted(on3C.gate, QStringLiteral("D-02")),
                 (QStringList{"command BT at byte 0"}));
        // The 3C frame is the port's own: the mock decodes it, a write of D50 outside scratch.
        QVERIFY(unaccounted(on3C.gate, QStringLiteral("D-03")).isEmpty());
        QCOMPARE(kindsOf(on3C.gate, QStringLiteral("D-03")),
                 (QStringList{"raw (decoded as a write)"}));
        const Verdict on1C = gateOf(steps, oneC);
        QCOMPARE(unaccounted(on1C.gate, QStringLiteral("D-01")),
                 (QStringList{"command 1401 at byte 0"}));
        QCOMPARE(unaccounted(on1C.gate, QStringLiteral("D-03")),
                 (QStringList{"command 1401 at byte 0"}));
        // The 1C frame is the port's own family, but the mock has no layout for BT.
        QCOMPARE(unaccounted(on1C.gate, QStringLiteral("D-02")),
                 (QStringList{"command BT at byte 0"}));

        // Ethernet: the same for the other data code and the other family.
        const Profile q = qProfile();
        const Profile a = profileForRow(QStringLiteral("3E-ascii"));
        const QByteArray ethernet =
            rawStepJson(QStringLiteral("E-01"),
                        spaced(frameQnaAscii(false, qnaBodyAscii(QStringLiteral("1401"))))) +
            ',' +
            rawStepJson(QStringLiteral("E-02"),
                        spaced(frame3EBin(qnaBodyBin(QStringLiteral("1402"))))) +
            ',' +
            rawStepJson(QStringLiteral("E-03"),
                        spaced(frame4EBin(qnaBodyBin(QStringLiteral("1401")))));
        const Verdict onBin = gateOf(ethernet, q);
        QCOMPARE(unaccounted(onBin.gate, QStringLiteral("E-01")),
                 (QStringList{"command 1401 at byte 0"}));
        // The port's own code and family, but a command the mock has no layout for.
        QCOMPARE(unaccounted(onBin.gate, QStringLiteral("E-02")),
                 (QStringList{"command 1402 at byte 0"}));
        const Verdict onAscii = gateOf(ethernet, a);
        QCOMPARE(unaccounted(onAscii.gate, QStringLiteral("E-02")),
                 (QStringList{"command 1402 at byte 0"}));
        QCOMPARE(unaccounted(onAscii.gate, QStringLiteral("E-03")),
                 (QStringList{"command 1401 at byte 0"}));
    }

    void HIL_02_aFrameCutOffAfterItsCommandIsRefusedWithoutReadOnlyToo() {
        // A09: nothing of the write is missing but its data, and the command is there. Declared
        // readOnly it is refused by the allow-list (the table above); not declared it is refused
        // because no write is decoded there.
        const QByteArray steps =
            rawStepJson(QStringLiteral("A-01"),
                        spaced(frame3EBin(qnaBodyBin(QStringLiteral("1402"))).left(13))) +
            ',' +
            rawStepJson(QStringLiteral("A-02"),
                        spaced(frame3EBin(qnaBodyBin(QStringLiteral("1401"))).left(13))) +
            ',' +
            rawStepJson(QStringLiteral("A-03"),
                        spaced(frame3EBin(qnaBodyBin(QStringLiteral("0401"))).left(13)));
        const Verdict v = gateOf(steps, qProfile());
        QCOMPARE(unaccounted(v.gate, QStringLiteral("A-01")),
                 (QStringList{"command 1402 at byte 0"}));
        QCOMPARE(unaccounted(v.gate, QStringLiteral("A-02")),
                 (QStringList{"command 1401 at byte 0"}));
        // A read cut off is a fragment: it needs readOnly and a recovery, but no command is wrong.
        QVERIFY(unaccounted(v.gate, QStringLiteral("A-03")).isEmpty());
        QCOMPARE(kindsOf(v.gate, QStringLiteral("A-03")), (QStringList{"raw"}));
    }

    void HIL_02_aWriteInsideScratchStillGoesThroughOnceAccountedFor() {
        // The default deny does not stop what the plans do: a write the mock decodes and that lies
        // in scratch, alone or behind a read, on both transports.
        const Profile q = qProfile();
        const Profile c24 = profileForRow(QStringLiteral("3C-F4"));
        const ByteBuf one = wordBytes({1});
        const auto write = [&](const Profile& p, uint32_t number) {
            return McProtocol(p.device.frame)
                .encode(Request::writeWords(Device{DeviceType::D, number},
                                            ByteView{one.data(), one.size()}))
                .value();
        };
        const auto read = [&](const Profile& p) {
            return McProtocol(p.device.frame)
                .encode(Request::readWords(Device{DeviceType::D, 100}, 1))
                .value();
        };
        for (const Profile* p : {&q, &c24}) {
            ByteBuf inside = read(*p);
            const ByteBuf w = write(*p, 150);
            inside.insert(inside.end(), w.begin(), w.end());
            ByteBuf outside = read(*p);
            const ByteBuf o = write(*p, 3000);
            outside.insert(outside.end(), o.begin(), o.end());
            const Verdict v = gateOf(rawStepJson(QStringLiteral("I-01"), hexOf(inside)) + ',' +
                                         rawStepJson(QStringLiteral("I-02"), hexOf(outside)),
                                     *p);
            QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
            QCOMPARE(stepsOf(v.gate), (QStringList{"I-02"}));
            QCOMPARE(kindsOf(v.gate, QStringLiteral("I-02")),
                     (QStringList{
                         QStringLiteral("raw (write embedded at byte %1)").arg(read(*p).size())}));
        }
    }

    // ---- HIL-02: a frameOverride cannot change how the gate reads the bytes --------------------

    void HIL_02_aFrameOverrideOfFrameCodeOrFormatIsRefusedOnRawAndMutateSteps() {
        // With {"code":"Ascii"} (or another frame, format or sum check) the gate would analyse
        // another family than the one the bytes are sent to. The two repros are binary 3E writes
        // of D50 (1401 and a random write 1402).
        const QString write1401 =
            QStringLiteral("50 00 00 FF FF 03 00 0E 00 10 00 01 14 00 00 32 00 00 A8 01 00 34 12");
        const QString random1402 =
            QStringLiteral("50 00 00 FF FF 03 00 0E 00 10 00 02 14 00 00 01 00 32 00 00 A8 07 00");
        struct Case {
            const char* profile;
            QString hex;
            const char* overrideJson;
            QString key;
        };
        const QString serialWrite =
            spaced(serialFrame(4, false, ascii("14010000D*0000320001") + ascii("0007")));
        const QVector<Case> cases{{"3E-bin", write1401, R"({"code":"Ascii"})", "code"},
                                  {"3E-bin", random1402, R"({"code":"Ascii"})", "code"},
                                  {"3E-bin", write1401, R"({"frame":"1E"})", "frame"},
                                  {"3E-bin", random1402, R"({"frame":"1E"})", "frame"},
                                  {"3E-ascii", write1401, R"({"code":"Binary"})", "code"},
                                  {"1E-bin", write1401, R"({"frame":"3E"})", "frame"},
                                  {"3C-F4", serialWrite, R"({"format":"Format3"})", "format"},
                                  {"3C-F4", serialWrite, R"({"format":"Format2"})", "format"},
                                  {"3C-F4", serialWrite, R"({"sumCheck":false})", "sumCheck"},
                                  {"3C-F4", serialWrite, R"({"frame":"1C"})", "frame"},
                                  {"1C-F1", serialWrite, R"({"frame":"3C"})", "frame"}};
        for (const Case& c : cases) {
            const Profile p = profileForRow(QString::fromLatin1(c.profile));
            QVERIFY2(!p.id.isEmpty(), c.profile);
            const char* recover = p.device.frame.isSerial() ? "eot" : "reconnect";
            const QByteArray raw = withOverride(
                readOnlyRawJson(QStringLiteral("O-01"), c.hex, recover), c.overrideJson);
            const QByteArray plain =
                withOverride(rawStepJson(QStringLiteral("O-01"), c.hex), c.overrideJson);
            for (const QByteArray& step : {raw, plain}) {
                const Verdict v = gateOf(step, p);
                const QString what = QString::fromLatin1(c.profile) + QLatin1Char(' ') +
                                     QString::fromLatin1(c.overrideJson);
                QVERIFY2(v.planError.isEmpty(), qPrintable(what + v.planError));
                QVERIFY2(v.resolved.ok(), qPrintable(what));
                QVERIFY2(kindsOf(v.gate, QStringLiteral("O-01"))
                             .contains(QStringLiteral("frameOverride")),
                         qPrintable(what + QLatin1Char(' ') + refusalText(v.gate)));
                bool named = false;
                for (const GateViolation& violation : v.gate.violations) {
                    named = named || (violation.kind == QStringLiteral("frameOverride") &&
                                      violation.range.split(QStringLiteral(", ")).contains(c.key));
                }
                QVERIFY2(named, qPrintable(what));
            }
        }
        // At least the 3E and the serial repros are really refused by the override rule: the plan
        // loads and resolves (nothing but the gate stops them).
        const Profile q = qProfile();
        const Verdict v =
            gateOf(withOverride(readOnlyRawJson(QStringLiteral("O-01"), write1401, "reconnect"),
                                R"({"code":"Ascii"})"),
                   q);
        QVERIFY2(v.resolved.ok(), "the repro must resolve");
        QVERIFY(v.gate.refused());
    }

    void HIL_02_aMutateMayNotChangeFrameCodeFormatSumCheckOrRoutingEither() {
        const QByteArray base = R"JSON({"id":"M-01","kind":"mutate",
            "request":{"kind":"read","device":"D@s"},"edits":[]})JSON";
        const Profile q = qProfile();
        const Profile c24 = profileForRow(QStringLiteral("3C-F4"));
        const auto refusedFor = [&](const Profile& p, const char* overrideJson) {
            const Verdict v = gateOf(withOverride(base, overrideJson), p);
            if (!v.planError.isEmpty() || !v.resolved.ok()) {
                return QStringList{QStringLiteral("not resolved: ") + v.planError};
            }
            for (const GateViolation& violation : v.gate.violations) {
                if (violation.kind == QStringLiteral("frameOverride")) {
                    return violation.range.split(QStringLiteral(", "));
                }
            }
            return QStringList();
        };
        QCOMPARE(refusedFor(q, R"({"code":"Ascii"})"), (QStringList{"code"}));
        QCOMPARE(refusedFor(q, R"({"station":1})"), (QStringList{"station"}));
        QCOMPARE(refusedFor(q, R"({"network":5,"pc":2})"), (QStringList{"network", "pc"}));
        QCOMPARE(refusedFor(q, R"({"io":1})"), (QStringList{"io"}));
        QCOMPARE(refusedFor(c24, R"({"stationNo":"+1"})"), (QStringList{"stationNo"}));
        QCOMPARE(refusedFor(c24, R"({"selfStation":1})"), (QStringList{"selfStation"}));
        QCOMPARE(refusedFor(c24, R"({"format":"Format1","sumCheck":false})"),
                 (QStringList{"format", "sumCheck"}));
        // The keys that do not change how the bytes are read stay allowed, and so does a key that
        // is written but keeps the profile's value.
        QCOMPARE(refusedFor(q, R"({"series":"IqR"})"), QStringList());
        QCOMPARE(refusedFor(q, R"({"timeoutMs":1500,"monitoringTimer":20})"), QStringList());
        QCOMPARE(refusedFor(q, R"({"station":0,"network":0,"code":"Binary"})"), QStringList());
        QCOMPARE(refusedFor(c24, R"({"blockNo":91,"f3ShortResponseHasSum":false})"), QStringList());
    }

    void HIL_02_aWriteMayNotChangeTheRoutingOfTheProfile() {
        // A write must reach the PLC whose scratch the profile declares. Reads may be sent to
        // another station (catalogue G7-04, G8-Q5), and the other override keys stay allowed.
        const Profile q = qProfile();
        const Verdict v = gateOf(R"JSON(
            {"id":"W-01","kind":"write","device":"D@s","values":[1],"frameOverride":{"station":1}},
            {"id":"W-02","kind":"write","device":"D@s","values":[1],"frameOverride":{"network":5}},
            {"id":"W-03","kind":"write","device":"D@s","values":[1],"frameOverride":{"pc":2}},
            {"id":"W-04","kind":"write","device":"D@s","values":[1],"frameOverride":{"io":1}},
            {"id":"W-05","kind":"write","device":"D@s","values":[1],
                "frameOverride":{"timeoutMs":900,"series":"IqR","station":0}},
            {"id":"R-01","kind":"read","device":"D@s","frameOverride":{"station":1}},
            {"id":"R-02","kind":"read","device":"D@s","frameOverride":{"network":5},
                "then":[{"kind":"write","device":"D@s","values":[1]}]},
            {"id":"P-01","kind":"poll","rounds":1,"heartbeat":"M@s",
                "subscribe":[{"name":"a","device":"D@s","count":1}],
                "frameOverride":{"network":5}},
            {"id":"P-02","kind":"poll","rounds":1,
                "subscribe":[{"name":"a","device":"D@s","count":1}],
                "frameOverride":{"network":5}},
            {"id":"P-03","kind":"poll","rounds":1,
                "subscribe":[{"name":"a","device":"D@s","count":1}],
                "actions":[{"after":1,"write":{"device":"D@s","values":[1]}}],
                "frameOverride":{"station":3}},
            {"id":"B-01","kind":"bench","request":{"kind":"write","device":"D@s","values":[1]},
                "reps":2,"frameOverride":{"station":1}},
            {"id":"B-02","kind":"bench","request":{"kind":"read","device":"D@s"},"reps":2,
                "frameOverride":{"station":1}},
            {"id":"B-03","kind":"bench","pollSet":"P-01","frameOverride":{"station":1}}
        )JSON",
                                 q);
        QVERIFY2(
            v.planError.isEmpty() && v.resolved.ok(),
            qPrintable(v.planError +
                       (v.resolved.errors.isEmpty() ? QString() : v.resolved.errors[0].text())));
        QCOMPARE(stepsOf(v.gate), (QStringList{"W-01", "W-02", "W-03", "W-04", "R-02", "P-01",
                                               "P-03", "B-01", "B-03"}));
        for (const GateViolation& violation : v.gate.violations) {
            QCOMPARE(violation.kind, QStringLiteral("frameOverride"));
        }
        QCOMPARE(v.gate.violations[0].range, QStringLiteral("station"));
        QCOMPARE(v.gate.violations[1].range, QStringLiteral("network"));
        QVERIFY(v.gate.violations[0].why.contains(QStringLiteral("scratch the profile declares")));

        // Serial: the station number of a 3C/1C access route is routing too.
        const Profile c24 = profileForRow(QStringLiteral("3C-F4"));
        const Verdict serial = gateOf(R"JSON(
            {"id":"S-01","kind":"write","device":"D@s","values":[1],
                "frameOverride":{"stationNo":"+1"}},
            {"id":"S-02","kind":"write","device":"D@s","values":[1],
                "frameOverride":{"selfStation":1}},
            {"id":"S-03","kind":"read","device":"D@s","frameOverride":{"stationNo":"+1"}}
        )JSON",
                                      c24);
        QCOMPARE(stepsOf(serial.gate), (QStringList{"S-01", "S-02"}));
    }

    void HIL_02_aPollRunsTheSessionHeartbeatSoItsRoutingIsFixedToo() {
        // The profile's own heartbeat is a write that every poll runs: a poll that has none of
        // its own still writes when the profile enables it.
        QJsonObject root = readJsonFile(exampleProfilePath(QStringLiteral("q03ude-eth-3e-bin")));
        QJsonObject device = root.value(QStringLiteral("device")).toObject();
        QJsonObject session = device.value(QStringLiteral("session")).toObject();
        QJsonObject heartbeat;
        heartbeat.insert(QStringLiteral("enabled"), true);
        heartbeat.insert(QStringLiteral("device"), QStringLiteral("M200"));
        session.insert(QStringLiteral("heartbeat"), heartbeat);
        device.insert(QStringLiteral("session"), session);
        root.insert(QStringLiteral("device"), device);
        const ProfileLoad load = loadProfile(root);
        QVERIFY2(load.ok(), "heartbeat profile");
        const QByteArray steps = R"JSON(
            {"id":"P-01","kind":"poll","rounds":1,
                "subscribe":[{"name":"a","device":"D@s","count":1}],
                "frameOverride":{"station":1}},
            {"id":"P-02","kind":"poll","rounds":1,
                "subscribe":[{"name":"a","device":"D@s","count":1}]},
            {"id":"B-01","kind":"bench","pollSet":"P-02","frameOverride":{"station":1}},
            {"id":"B-02","kind":"bench","request":{"kind":"read","device":"D@s"},"reps":2,
                "frameOverride":{"station":1}})JSON";
        const Verdict with = gateOf(steps, *load.profile);
        QVERIFY2(with.planError.isEmpty() && with.resolved.ok(), qPrintable(with.planError));
        QCOMPARE(stepsOf(with.gate), (QStringList{"P-01", "B-01"}));
        // The same plan on a profile without the heartbeat changes nothing for the reads.
        QVERIFY(gateOf(steps, qProfile()).gate.violations.isEmpty());
    }

    void HIL_02_aSkippedStepsOverrideIsNotChecked() {
        const Verdict v = gateOf(R"JSON(
            {"id":"K-01","kind":"write","device":"D@s","values":[1],
                "requires":["profile:no-such-profile"],"frameOverride":{"station":1}})JSON",
                                 qProfile());
        QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
        QVERIFY(v.resolved.steps[0].skipped());
        QVERIFY(!v.gate.refused());
    }

    void HIL_02_everyReadOnlyFrameOfTheCommittedPlansIsAdmitted() {
        struct Pair {
            QString plan;    // relative to tests/hil
            QString profile; // an example profile id, or "fixtures/<file>"
        };
        const QVector<Pair> pairs{
            {"plans/qna_ethernet.json", "q03ude-eth-3e-bin"},
            {"plans/qna_ethernet.json", "fx5u-eth-3e-ascii"},
            {"plans/a1e.json", "fx3-eth-1e-bin"},
            {"plans/qna_serial.json", "q03ude-c24-3c-f4"},
            {"plans/a1c.json", "fx3-serial-1c-f1"},
            {"e2e/plan_3e.json", "q03ude-eth-3e-bin"},
            {"e2e/plan_3c.json", "q03ude-c24-3c-f4"},
            {"e2e/plan_bench.json", "q03ude-eth-3e-bin"},
            {"e2e/plan_fault.json", "q03ude-eth-3e-bin"},
            {"fixtures/plans/fixture_3e.json", "fixtures/vplc-3e-bin.json"},
            {"fixtures/plans/fixture_3e_drop.json", "fixtures/vplc-3e-bin-drop.json"},
            {"fixtures/plans/fixture_3e_fault.json", "fixtures/vplc-3e-bin-fault.json"},
            {"fixtures/plans/fixture_3c.json", "fixtures/vplc-3c-f4.json"}};
        int readOnlyFrames = 0;
        for (const Pair& pair : pairs) {
            const PlanLoad plan = loadPlanFile(testsDir() + QStringLiteral("/hil/") + pair.plan);
            QVERIFY2(plan.ok(), qPrintable(pair.plan + QStringLiteral(": ") + plan.error.text()));
            Profile profile;
            if (pair.profile.startsWith(QStringLiteral("fixtures/"))) {
                const ProfileLoad load = loadProfileFile(
                    testsDir() + QStringLiteral("/hil/fixtures/profiles/") + pair.profile.mid(9));
                QVERIFY2(load.ok(), qPrintable(pair.profile));
                profile = *load.profile;
            } else {
                profile = loadExample(pair.profile);
            }
            const ResolveResult resolved = resolvePlan(*plan.plan, profile);
            QVERIFY2(resolved.ok(), qPrintable(pair.plan));
            const GateReport gate = checkGate(resolved, profile);
            QVERIFY2(!gate.refused(),
                     qPrintable(pair.plan + QStringLiteral(" with ") + pair.profile +
                                QStringLiteral(": ") + refusalText(gate)));
            for (const ResolvedStep& step : resolved.steps) {
                for (const ResolvedOp& op : step.ops) {
                    if (!step.skipped() && op.via != Via::Api && op.readOnly) {
                        ++readOnlyFrames;
                    }
                }
            }
        }
        // Not a vacuous pass: the plans really hold readOnly frames, counted over the pairs above
        // once the steps that do not apply to a profile are skipped. A plan that loses one fails
        // here on purpose.
        QCOMPARE(readOnlyFrames, 17);
    }

    void HIL_02_theCommittedPlansStayAdmittedInEveryFrameVariant() {
        // The example profiles are one format each; the plans must pass the gate in the others too
        // (a read-only frame of the plan is a read in every format).
        struct Variant {
            QString plan;
            QString example;
            QJsonObject keys;
        };
        const auto keys = [](const char* format, const char* code, const char* commandSet) {
            QJsonObject o;
            o.insert(QStringLiteral("format"), QLatin1String(format));
            o.insert(QStringLiteral("code"), QLatin1String(code));
            o.insert(QStringLiteral("commandSet"), QLatin1String(commandSet));
            return o;
        };
        QVector<Variant> variants;
        for (const char* format : {"Format1", "Format2", "Format3", "Format4"}) {
            variants.push_back({"qna_serial", "q03ude-c24-3c-f4", keys(format, "Ascii", "ACPU")});
            variants.push_back({"a1c", "fx3-serial-1c-f1", keys(format, "Ascii", "ACPU")});
            variants.push_back({"a1c", "fx3-serial-1c-f1", keys(format, "Ascii", "AnA")});
        }
        QJsonObject ascii1E;
        ascii1E.insert(QStringLiteral("code"), QStringLiteral("Ascii"));
        variants.push_back({"a1e", "fx3-eth-1e-bin", ascii1E});
        QJsonObject sumOff;
        sumOff.insert(QStringLiteral("sumCheck"), false);
        variants.push_back({"qna_serial", "q03ude-c24-3c-f4", sumOff});
        variants.push_back({"a1c", "fx3-serial-1c-f1", sumOff});
        for (const Variant& variant : variants) {
            const PlanLoad plan = loadPlanFile(testsDir() + QStringLiteral("/hil/plans/") +
                                               variant.plan + QStringLiteral(".json"));
            QVERIFY(plan.ok());
            const Profile profile = withFrameKeys(variant.example, variant.keys);
            QVERIFY2(!profile.id.isEmpty(), qPrintable(variant.example));
            const GateReport gate = checkGate(resolvePlan(*plan.plan, profile), profile);
            QVERIFY2(!gate.refused(),
                     qPrintable(variant.plan + QStringLiteral(" with ") +
                                QString::fromUtf8(QJsonDocument(variant.keys).toJson()) +
                                refusalText(gate)));
        }
    }

    void HIL_02_aWriteJustOutsideScratchIsRefusedOnBothSides() {
        // Scratch is D100-D2099, M100-M2099, W100-W1FF (hex). Every edge, worked by hand.
        const Verdict v = gateOf(R"JSON(
            {"id":"E-01","kind":"write","device":"D99","values":[1]},
            {"id":"E-02","kind":"write","device":"D100","values":[1]},
            {"id":"E-03","kind":"write","device":"D2099","values":[1]},
            {"id":"E-04","kind":"write","device":"D2100","values":[1]},
            {"id":"E-05","kind":"write","device":"D2099","count":2,"values":[1,2]},
            {"id":"E-06","kind":"write","device":"D99","count":2,"values":[1,2]},
            {"id":"E-07","kind":"write","device":"M99","unit":"bit","values":"1"},
            {"id":"E-08","kind":"write","device":"M2099","unit":"bit","values":"1"},
            {"id":"E-09","kind":"write","device":"M2100","unit":"bit","values":"1"},
            {"id":"E-10","kind":"write","device":"WFF","values":[1]},
            {"id":"E-11","kind":"write","device":"W100","values":[1]},
            {"id":"E-12","kind":"write","device":"W1FF","values":[1]},
            {"id":"E-13","kind":"write","device":"W200","values":[1]},
            {"id":"E-14","kind":"write","device":"W1FF","count":2,"values":[1,2]}
        )JSON",
                                 qProfile());
        QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
        QCOMPARE(stepsOf(v.gate), (QStringList{"E-01", "E-04", "E-05", "E-06", "E-07", "E-09",
                                               "E-10", "E-13", "E-14"}));
    }

    void HIL_02_aWriteOutsideScratchInAFrameIsRefusedForBitsAndWords() {
        const Profile q = qProfile();
        const McProtocol codec(q.device.frame);
        const ByteBuf bit = {1};
        const ByteBuf word = wordBytes({1});
        const auto hexWrite = [&](const Request& r) { return hexOf(codec.encode(r).value()); };
        const QString bitOutside = hexWrite(
            Request::writeBits(Device{DeviceType::M, 5000}, ByteView{bit.data(), bit.size()}));
        const QString bitInside = hexWrite(
            Request::writeBits(Device{DeviceType::M, 150}, ByteView{bit.data(), bit.size()}));
        const QString wordOutside = hexWrite(
            Request::writeWords(Device{DeviceType::D, 5000}, ByteView{word.data(), word.size()}));
        // A word write to a bit device covers 16 numbers per word: M2090..M2105 runs past M2099,
        // and M2080..M2095 does not.
        const QString coversPast = hexWrite(
            Request::writeWords(Device{DeviceType::M, 2090}, ByteView{word.data(), word.size()}));
        const QString coversInside = hexWrite(
            Request::writeWords(Device{DeviceType::M, 2080}, ByteView{word.data(), word.size()}));
        const QByteArray steps = rawStepJson(QStringLiteral("B-01"), bitOutside) + ',' +
                                 rawStepJson(QStringLiteral("B-02"), bitInside) + ',' +
                                 rawStepJson(QStringLiteral("B-03"), wordOutside) + ',' +
                                 rawStepJson(QStringLiteral("B-04"), coversPast) + ',' +
                                 rawStepJson(QStringLiteral("B-05"), coversInside) + ',' +
                                 QByteArray(R"JSON(
            {"id":"B-06","kind":"mutate","request":{"kind":"write","device":"M5000","unit":"bit",
                "values":"1"}},
            {"id":"B-07","kind":"mutate","request":{"kind":"write","device":"M@s","unit":"bit",
                "values":"1"}})JSON");
        const Verdict v = gateOf(steps, q);
        QVERIFY2(v.planError.isEmpty() && v.resolved.ok(), qPrintable(v.planError));
        QCOMPARE(stepsOf(v.gate), (QStringList{"B-01", "B-03", "B-04", "B-06"}));
        QCOMPARE(v.gate.violations[0].range, QStringLiteral("M5000-M5000 (1 point)"));
        QCOMPARE(v.gate.violations[2].range, QStringLiteral("M2090-M2105 (16 points)"));
        QVERIFY2(v.gate.violations[2].why.contains(
                     QStringLiteral("runs past the end of scratch range M100-M2099")),
                 qPrintable(v.gate.violations[2].why));
    }

    void HIL_02_yesDoesNotSkipTheConfirmationWhenReadOnlyFramesExist() {
        const QString dir = scratchDir(QStringLiteral("gate_yes"));
        QTcpServer server; // a port that is free now and closed when the server goes
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const quint16 port = server.serverPort();
        server.close();
        QFile withFrame(dir + QStringLiteral("/with.json"));
        QVERIFY(withFrame.open(QIODevice::WriteOnly));
        withFrame.write(planWith(
            R"({"id":"A-01","kind":"read","device":"D@s"},{"id":"A-02","kind":"raw","hex":"DE AD",
                "readOnly":true,"recover":"reconnect"})"));
        withFrame.close();
        QFile without(dir + QStringLiteral("/without.json"));
        QVERIFY(without.open(QIODevice::WriteOnly));
        without.write(planWith(R"({"id":"A-01","kind":"read","device":"D@s"})"));
        without.close();
        Options options;
        options.profilePath = writeLoopbackProfile(dir, port);
        options.outputRoot = dir + QStringLiteral("/out");
        options.reconnectSeconds = 1;
        options.yes = true;

        // With a read-only frame: --yes is not enough, the profile id must be typed.
        options.planPath = dir + QStringLiteral("/with.json");
        ToolRun run = runToolWith(options);
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::BadInput));
        QVERIFY(run.err.contains(QStringLiteral("not confirmed; nothing was sent")));
        QVERIFY(run.out.contains(QStringLiteral("--yes does not apply")));
        QVERIFY(run.out.contains(QStringLiteral("Type the profile id (q03ude-eth-3e-bin)")));
        run = runToolWith(options, QStringLiteral("not the id\n"));
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::BadInput));
        QVERIFY(!QDir(dir + QStringLiteral("/out")).exists());
        // The right id goes on to run (and fails to connect: nothing listens).
        run = runToolWith(options, QStringLiteral("q03ude-eth-3e-bin\n"));
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::RunFailures));

        // Without one, --yes skips the prompt as before.
        options.planPath = dir + QStringLiteral("/without.json");
        run = runToolWith(options);
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::RunFailures));
        QVERIFY(!run.out.contains(QStringLiteral("Type the profile id")));
    }

    void HIL_02_serialFramesAreDecodedWithTheirFormat() {
        const Profile c24 = loadExample(QStringLiteral("q03ude-c24-3c-f4"));
        const McProtocol codec(c24.device.frame);
        const ByteBuf bytes = wordBytes({1});
        const ByteBuf outside =
            codec
                .encode(Request::writeWords(Device{DeviceType::D, 3000},
                                            ByteView{bytes.data(), bytes.size()}))
                .value();
        const QByteArray steps = rawStepJson(QStringLiteral("C-01"), hexOf(outside)) + ',' +
                                 rawStepJson(QStringLiteral("C-02"), QStringLiteral("{EOT}")) +
                                 ',' + QByteArray(R"JSON(
            {"id":"C-03","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"replaceSum":{"add":1}}]},
            {"id":"C-04","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "frameOverride":{"stationNo":"+1"}})JSON");
        const Verdict v = gateOf(steps, c24);
        QVERIFY2(v.resolved.ok(),
                 qPrintable(v.resolved.errors.isEmpty() ? QString() : v.resolved.errors[0].text()));
        // An EOT is understood and a bad SUM decodes as a read. C-04 sends the read to another
        // station: a mutate may not change the routing of the profile.
        QCOMPARE(stepsOf(v.gate), (QStringList{"C-01", "C-04"}));
        QCOMPARE(kindsOf(v.gate, QStringLiteral("C-04")), (QStringList{"frameOverride"}));
        QCOMPARE(v.gate.violations[1].range, QStringLiteral("stationNo"));
    }

    void HIL_02_benchStepsAreCheckedLikeWrites() {
        const Verdict v = gateOf(R"JSON(
            {"id":"B-01","kind":"bench","request":{"kind":"write","device":"D3000","values":[1]},
                "reps":2},
            {"id":"B-02","kind":"bench","request":{"kind":"write","device":"D@s","values":[1]},
                "reps":2},
            {"id":"B-03","kind":"bench","request":{"kind":"read","device":"D3000"},"reps":2},
            {"id":"G6","kind":"poll","rounds":1,"subscribe":[{"name":"far","device":"D3000",
                "count":4}]},
            {"id":"B-04","kind":"bench","pollSet":"G6"}
        )JSON",
                                 qProfile());
        // G6 itself is a poll step of the plan, so it is refused too.
        QCOMPARE(stepsOf(v.gate), (QStringList{"B-01", "G6", "B-04"}));
        QCOMPARE(v.gate.violations[0].kind, QStringLiteral("bench write"));
    }

    void HIL_02_oneRunListsEveryOffender() {
        const Verdict v = gateOf(R"JSON(
            {"id":"X-01","kind":"write","device":"D3000","values":[1]},
            {"id":"X-02","kind":"poll","rounds":1,"subscribe":[{"name":"a","device":"D@s",
                "count":1}],
             "actions":[{"after":1,"write":{"device":"D3001","values":[1]}}]},
            {"id":"X-03","kind":"mutate","request":{"kind":"write","device":"D@s","values":[1]},
             "edits":[{"setByte":{"at":16,"value":"0x20"}}]},
            {"id":"X-04","kind":"raw","hex":"DE AD"},
            {"id":"X-05","kind":"write","device":"D@s","values":[1]}
        )JSON",
                                 qProfile());
        QCOMPARE(stepsOf(v.gate), (QStringList{"X-01", "X-02", "X-03", "X-04"}));
        const QString text = refusalText(v.gate);
        for (const char* id : {"X-01", "X-02", "X-03", "X-04"}) {
            QVERIFY2(text.contains(QLatin1String(id)), id);
        }
        QVERIFY(!text.contains(QStringLiteral("X-05")));
        QVERIFY(text.contains(QStringLiteral("4 violation(s)")));
        QVERIFY(text.contains(QStringLiteral("nothing was sent")));
    }

    void HIL_02_skippedStepsSendNothingAndAreNotChecked() {
        const Verdict v = gateOf(R"JSON(
            {"id":"K-01","kind":"write","device":"D3000","values":[1],"requires":["!supports:D"]},
            {"id":"K-02","kind":"write","device":"M@s16","unit":"word","count":"WBWmax",
                "values":{"gen":"fill","value":0},"scratchTooSmall":"skip"}
        )JSON",
                                 qProfile());
        QVERIFY(v.resolved.steps[0].skipped() && v.resolved.steps[1].skipped());
        QVERIFY(!v.gate.refused());
    }

    void HIL_02_aProfileHeartbeatIsInTheGate() {
        // The profile loader refuses a heartbeat outside scratch already; the gate checks it again
        // for a Profile built in code.
        Profile p = qProfile();
        p.device.session.heartbeat.enabled = true;
        p.device.session.heartbeat.device = Device{DeviceType::M, 5000};
        const Verdict v = gateOf(R"JSON({"id":"H-01","kind":"read","device":"D@s"})JSON", p);
        QCOMPARE(stepsOf(v.gate), (QStringList{"(profile)"}));
        QCOMPARE(v.gate.violations[0].kind, QStringLiteral("session heartbeat"));
    }

    void HIL_02_aRefusedRunSendsNothingEvenWithYes() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QString dir = scratchDir(QStringLiteral("gate_refused"));
        const QString profilePath = writeLoopbackProfile(dir, server.serverPort());
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(planWith(
            R"({"id":"B-01","kind":"read","device":"D@s"},{"id":"B-02","kind":"write",
                "device":"D3000","values":[1]})"));
        planFile.close();

        Options options;
        options.profilePath = profilePath;
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/out");
        for (const bool yes : {false, true}) {
            for (const bool dryRun : {false, true}) {
                options.yes = yes;
                options.dryRun = dryRun;
                const ToolRun run = runToolWith(options, QStringLiteral("q03ude-eth-3e-bin\n"));
                QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::GateRefused));
                QVERIFY(run.err.contains(QStringLiteral("SAFETY GATE: run REFUSED")));
                QVERIFY(run.err.contains(QStringLiteral("B-02")));
                QVERIFY(!run.err.contains(QStringLiteral("B-01")));
                QVERIFY(!run.out.contains(QStringLiteral("tx ")));
            }
        }
        // Nothing connected, nothing was written under the output root.
        QVERIFY(!server.waitForNewConnection(300));
        QVERIFY(!server.hasPendingConnections());
        QVERIFY(!QDir(dir + QStringLiteral("/out")).exists());
    }

    // ---- confirmation -------------------------------------------------------------------------

    void HIL_02_confirmationSummaryAndPrompt() {
        const QString dir = scratchDir(QStringLiteral("gate_prompt"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(planWith(
            R"({"id":"A-01","kind":"read","device":"D@s"},{"id":"A-02","kind":"raw",
                "hex":"DE AD BE EF","readOnly":true,"recover":"reconnect"},{"id":"A-03",
                "kind":"read","device":"ZR@s","requires":["scratch:ZR"]})"));
        planFile.close();
        Options options;
        options.profilePath = exampleProfilePath(QStringLiteral("q03ude-eth-3e-bin"));
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/out");

        ToolRun run = runToolWith(options, QStringLiteral("not the id\n"));
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::BadInput));
        QVERIFY(run.err.contains(QStringLiteral("not confirmed; nothing was sent")));
        QVERIFY(run.out.contains(QStringLiteral("Q03UDECPU")));           // PLC identity
        QVERIFY(run.out.contains(QStringLiteral("tcp 192.0.2.10:5000"))); // transport
        QVERIFY(run.out.contains(QStringLiteral("D100-D2099")));          // scratch area
        QVERIFY(run.out.contains(QStringLiteral("DE AD BE EF")));         // read-only frame
        QVERIFY(run.out.contains(QStringLiteral("Type the profile id (q03ude-eth-3e-bin)")));
        QVERIFY(run.out.contains(QStringLiteral("1 skipped")));

        // An empty answer (closed stdin) also aborts.
        run = runToolWith(options, QString());
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::BadInput));

        // The summary, with the read-only frames the plan declares, is printed before the prompt.
        // (--yes skips only the typing, and not even that while read-only frames exist: see
        // HIL_02_yesDoesNotSkipTheConfirmationWhenReadOnlyFramesExist.)
        QVERIFY(run.out.contains(QStringLiteral("Read-only frames the gate could not decode")));
    }

    // ---- HIL-03: --dry-run --------------------------------------------------------------------

    void HIL_03_dryRunPrintsExactlyTheEncodedFrames() {
        const Profile q = qProfile();
        const QString dir = scratchDir(QStringLiteral("dry_run"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(planWith(R"JSON(
            {"id":"P-01","kind":"write","device":"D@s","values":[1,2,3],"readBack":true},
            {"id":"P-02","kind":"read","device":"D@s","count":"Wmax+40"},
            {"id":"P-03","kind":"write","device":"M@s","values":"11001100"},
            {"id":"P-04","kind":"read","device":"M@s16","unit":"word","count":4},
            {"id":"P-05","kind":"mutate","request":{"kind":"read","device":"D@s","count":2},
                "edits":[{"truncate":2}],"readOnly":true,"recover":"reconnect"},
            {"id":"P-06","kind":"raw","hex":"DE AD","readOnly":true,"recover":"reconnect"},
            {"id":"P-07","kind":"poll","heartbeat":"M@s+200","rounds":2,
             "subscribe":[{"name":"d","device":"D@s","count":4},{"name":"m","device":"M@s",
                 "count":16},{"name":"x","device":"X0","count":32,"input":true}],
             "actions":[{"after":1,"write":{"device":"D@s+1","values":[7]}}]},
            {"id":"P-08","kind":"bench","request":{"kind":"read","device":"D@s","count":64},
                "reps":3}
        )JSON"));
        planFile.close();
        Options options;
        options.profilePath = exampleProfilePath(QStringLiteral("q03ude-eth-3e-bin"));
        options.planPath = dir + QStringLiteral("/plan.json");
        options.dryRun = true;
        const ToolRun run = runToolWith(options);
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::Ok));
        QVERIFY2(run.err.isEmpty(), qPrintable(run.err));

        // The expected frames, encoded here by McProtocol, independently of the tool.
        const McProtocol codec(q.device.frame);
        const auto enc = [&](const Request& r) { return hexOf(codec.encode(r).value()); };
        const ByteBuf three = wordBytes({1, 2, 3});
        const ByteBuf bits = {1, 1, 0, 0, 1, 1, 0, 0};
        const ByteBuf one = wordBytes({7});
        const ByteBuf hbOn = {1};
        const ByteBuf hbOff = {0};
        QStringList expected;
        expected << enc(
            Request::writeWords(Device{DeviceType::D, 100}, ByteView{three.data(), three.size()}));
        expected << enc(Request::readWords(Device{DeviceType::D, 100}, 3)); // read-back
        expected << enc(
            Request::readWords(Device{DeviceType::D, 100}, 960)); // 1000 words: two commands
        expected << enc(Request::readWords(Device{DeviceType::D, 1060}, 40));
        expected << enc(
            Request::writeBits(Device{DeviceType::M, 100}, ByteView{bits.data(), bits.size()}));
        expected << enc(Request::readWords(Device{DeviceType::M, 112}, 4));
        ByteBuf truncated = codec.encode(Request::readWords(Device{DeviceType::D, 100}, 2)).value();
        truncated.resize(truncated.size() - 2);
        expected << hexOf(truncated);
        expected << QStringLiteral("DE AD");
        expected << enc(
            Request::writeBits(Device{DeviceType::M, 300}, ByteView{hbOn.data(), hbOn.size()}));
        expected << enc(
            Request::writeBits(Device{DeviceType::M, 300}, ByteView{hbOff.data(), hbOff.size()}));
        RangeSet set;
        QVERIFY(set.add(Device{DeviceType::D, 100}, 4));
        QVERIFY(set.add(Device{DeviceType::M, 100}, 16));
        QVERIFY(set.add(Device{DeviceType::X, 0}, 32));
        const Expected<ReadPlan> plan = ReadPlan::build(set, q.device.frame, q.device.session.plan);
        QVERIFY(plan.hasValue());
        QVERIFY(plan.value().size() >= 3);
        for (size_t i = 0; i < plan.value().size(); ++i) {
            expected << enc(plan.value().chunk(i).request);
        }
        expected << enc(
            Request::writeWords(Device{DeviceType::D, 101}, ByteView{one.data(), one.size()}));
        expected << enc(Request::readWords(Device{DeviceType::D, 100}, 64));

        const QStringList printed = txLines(run.out);
        QCOMPARE(printed.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i) {
            QVERIFY2(printed[i] == expected[i],
                     qPrintable(QStringLiteral("frame %1: printed %2, expected %3")
                                    .arg(i)
                                    .arg(printed[i], expected[i])));
        }
        QVERIFY(run.out.contains(QStringLiteral("safety gate OK")));
        QVERIFY(run.out.contains(QStringLiteral("(read-back)")));
        QVERIFY(run.out.contains(QStringLiteral("(declared readOnly)")));
        QVERIFY(run.out.contains(QStringLiteral("HEARTBEAT M300 = 1")));
    }

    void HIL_03_dryRunShowsFramesOfEveryFamily() {
        // Each example profile: one read and one write, compared with an independent encode.
        for (const char* id :
             {"q03ude-c24-3c-f4", "fx5u-eth-3e-ascii", "fx3-eth-1e-bin", "fx3-serial-1c-f1"}) {
            const Profile p = loadExample(QLatin1String(id));
            const QString dir = scratchDir(QStringLiteral("dry_run_") + QLatin1String(id));
            QFile planFile(dir + QStringLiteral("/plan.json"));
            QVERIFY(planFile.open(QIODevice::WriteOnly));
            planFile.write(planWith(
                R"({"id":"F-01","kind":"write","device":"D@s","values":[4660,2]},{"id":"F-02",
                    "kind":"read","device":"M@s","count":8})"));
            planFile.close();
            Options options;
            options.profilePath = exampleProfilePath(QLatin1String(id));
            options.planPath = dir + QStringLiteral("/plan.json");
            options.dryRun = true;
            const ToolRun run = runToolWith(options);
            QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::Ok));
            const McProtocol codec(p.device.frame);
            const ByteBuf data = wordBytes({4660, 2});
            const uint32_t d = p.firstScratch(DeviceType::D)->first;
            const uint32_t m = p.firstScratch(DeviceType::M)->first;
            const QStringList expected{
                hexOf(codec
                          .encode(Request::writeWords(Device{DeviceType::D, d},
                                                      ByteView{data.data(), data.size()}))
                          .value()),
                hexOf(codec.encode(Request::readBits(Device{DeviceType::M, m}, 8)).value())};
            QCOMPARE(txLines(run.out), expected);
        }
    }

    void HIL_03_dryRunNamesWhatTheLibraryWouldRefuse() {
        const QString dir = scratchDir(QStringLiteral("dry_run_notsent"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        // 1E refuses word access to a bit device whose head is not a multiple of 16 (DEV-12).
        planFile.write(planWith(
            R"({"id":"N-01","kind":"read","device":"M@s16+3","unit":"word","expect":"notSent"},
                {"id":"N-02","kind":"read","device":"D@s","requires":["!supports:D"]})"));
        planFile.close();
        Options options;
        options.profilePath = exampleProfilePath(QStringLiteral("fx3-eth-1e-bin"));
        options.planPath = dir + QStringLiteral("/plan.json");
        options.dryRun = true;
        const ToolRun run = runToolWith(options);
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::Ok));
        QVERIFY(run.out.contains(QStringLiteral("not sent:")));
        QVERIFY(run.out.contains(QStringLiteral("STEP N-02 [read] skipped: requires !supports:D")));
        QVERIFY(txLines(run.out).isEmpty());
    }

    void HIL_03_dryRunHonoursOnlyGroups() {
        const QString dir = scratchDir(QStringLiteral("dry_run_only"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(planWith(
            R"({"id":"G1-01","kind":"read","device":"D@s"},{"id":"G2-01","kind":"read",
                "device":"D@s+1"},{"id":"G2-02","kind":"write","device":"D3000","values":[1]})"));
        planFile.close();
        Options options;
        options.profilePath = exampleProfilePath(QStringLiteral("q03ude-eth-3e-bin"));
        options.planPath = dir + QStringLiteral("/plan.json");
        options.dryRun = true;
        options.only = QStringList{QStringLiteral("G1")};
        const ToolRun run = runToolWith(options);
        // G2-02 would be refused, but only G1 is selected: the gate checks what will run.
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::Ok));
        QCOMPARE(txLines(run.out).size(), 1);
        QVERIFY(!run.out.contains(QStringLiteral("G2-01")));
    }

    void HIL_03_anOnlyGroupThatNoStepHasIsRefused() {
        const QString dir = scratchDir(QStringLiteral("dry_run_only_unknown"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(planWith(
            R"({"id":"G1-01","kind":"read","device":"D@s"},{"id":"G2-01","kind":"read",
                "device":"D@s+1"})"));
        planFile.close();
        Options options;
        options.profilePath = exampleProfilePath(QStringLiteral("q03ude-eth-3e-bin"));
        options.planPath = dir + QStringLiteral("/plan.json");
        options.dryRun = true;
        // A typo would select no step: a capture of nothing must not look like a clean run.
        options.only = QStringList{QStringLiteral("G9")};
        ToolRun run = runToolWith(options);
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::BadInput));
        QVERIFY2(run.err.contains(QStringLiteral("--only G9")), qPrintable(run.err));
        options.only = QStringList{QStringLiteral("G1"), QStringLiteral("G9")};
        QCOMPARE(static_cast<int>(runToolWith(options).code), static_cast<int>(ExitCode::BadInput));
        // Group ids are matched without regard to case, as the selection itself is.
        options.only = QStringList{QStringLiteral("g1"), QStringLiteral("G2")};
        QCOMPARE(static_cast<int>(runToolWith(options).code), static_cast<int>(ExitCode::Ok));
    }

    void HIL_03_badInputsExitWithCode2() {
        Options options;
        options.profilePath = testsDir() + QStringLiteral("/hil/profiles/none.json");
        options.planPath = QStringLiteral("none.json");
        options.dryRun = true;
        ToolRun run = runToolWith(options);
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::BadInput));
        QVERIFY(run.err.contains(QStringLiteral("cannot open")));

        options.profilePath = exampleProfilePath(QStringLiteral("q03ude-eth-3e-bin"));
        run = runToolWith(options);
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::BadInput));

        const QString dir = scratchDir(QStringLiteral("dry_run_bad"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(planWith(R"({"id":"Z-01","kind":"write","device":"ZR@s","values":[1]})"));
        planFile.close();
        options.planPath = dir + QStringLiteral("/plan.json");
        run = runToolWith(options);
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::BadInput));
        QVERIFY2(run.err.contains(QStringLiteral("Z-01: ")), qPrintable(run.err));
    }
};

} // namespace

QObject* makeGateSuite() { return new HilGateTests; }

} // namespace mc::hil::test

#include "tst_hil_gate.moc"
