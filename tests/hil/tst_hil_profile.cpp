// HIL-01 (SPEC-hil-capture.md): profile parsing, plan parsing, device reference resolution and the
// command line. No hardware, no socket.
#include "hil_suites.h"
#include "hil_test_support.h"

#include "hil_capture/options.h"
#include "hil_capture/plan.h"
#include "hil_capture/profile.h"
#include "hil_capture/resolve.h"
#include "hil_capture/safety_gate.h"
#include "mc/core/convert.h"
#include "mc/core/protocol.h"

#include <QJsonArray>
#include <QStringList>
#include <QtTest>

namespace mc::hil::test {

namespace {

const char* const kExampleIds[] = {"q03ude-eth-3e-bin", "q03ude-c24-3c-f4", "fx5u-eth-3e-ascii",
                                   "fx3-eth-1e-bin", "fx3-serial-1c-f1"};

void setAt(QJsonObject& o, const QStringList& path, int i, const QJsonValue& v) {
    if (i == path.size() - 1) {
        if (v.isUndefined()) {
            o.remove(path[i]);
        } else {
            o.insert(path[i], v);
        }
        return;
    }
    QJsonObject child = o.value(path[i]).toObject();
    setAt(child, path, i + 1, v);
    o.insert(path[i], child);
}

// The example profile's JSON with the value at the dotted path replaced (Undefined removes it).
QJsonObject edited(const QString& id, const QString& dottedPath, const QJsonValue& v) {
    QJsonObject root = readJsonFile(exampleProfilePath(id));
    setAt(root, dottedPath.split(QLatin1Char('.')), 0, v);
    return root;
}

QJsonObject exampleRoot(const QString& id = QStringLiteral("q03ude-eth-3e-bin")) {
    return readJsonFile(exampleProfilePath(id));
}

PlanLoad planFrom(const char* text) { return loadPlan(parseJson(text)); }

// "{ ... }" -> one-step plan JSON text
QByteArray planWith(const QByteArray& steps) {
    return QByteArray(R"JSON({"schema":1,"plan":{"id":"t"},"steps":[)JSON") + steps + "]}";
}

ResolveResult resolveText(const QByteArray& steps, const Profile& profile,
                          const QStringList& only = QStringList()) {
    const PlanLoad plan = loadPlan(QJsonDocument::fromJson(planWith(steps)).object());
    if (!plan.ok()) {
        ResolveResult r;
        r.errors.push_back(StepError{QStringLiteral("(plan)"), plan.error.text()});
        return r;
    }
    return resolvePlan(*plan.plan, profile, only);
}

QByteArray toBytes(const ByteBuf& b) {
    return QByteArray(reinterpret_cast<const char*>(b.data()), static_cast<int>(b.size()));
}

class HilProfileTests : public QObject {
    Q_OBJECT

  private slots:
    // ---- HIL-01: profiles ---------------------------------------------------------------------

    void HIL_01_exampleProfilesLoad() {
        for (const char* id : kExampleIds) {
            const ProfileLoad load = loadProfileFile(exampleProfilePath(QLatin1String(id)));
            QVERIFY2(load.ok(),
                     qPrintable(QLatin1String(id) + QStringLiteral(": ") + load.error.text()));
            QCOMPARE(load.profile->id, QString::fromLatin1(id));
            QVERIFY(!load.profile->scratch.isEmpty());
            QVERIFY(load.profile->device.validate());
        }
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        QCOMPARE(q.end(DeviceType::W).value_or(0), 0x1FFFu); // hexadecimal, from a string
        QCOMPARE(q.end(DeviceType::D).value_or(0), 12287u);
        QVERIFY(q.inScratch(DeviceType::W, 0x100, 0x100));
        QVERIFY(!q.inScratch(DeviceType::W, 0x100, 0x101));
        const Profile fx3 = loadExample(QStringLiteral("fx3-eth-1e-bin"));
        QCOMPARE(deviceText(fx3.specialBit, XyNumbering::Hex), QStringLiteral("M8000"));
        QCOMPARE(deviceText(fx3.specialWord, XyNumbering::Hex), QStringLiteral("D8000"));
        QVERIFY(fx3.device.frame.frame == FrameType::F1E);
        const Profile c24 = loadExample(QStringLiteral("q03ude-c24-3c-f4"));
        QVERIFY(c24.device.transport == TransportKind::Serial);
        QVERIFY(c24.device.frame.format == SerialFormat::Format4);
    }

    void HIL_01_examplesUsePlaceholderAddressesOnly() {
        for (const char* id : kExampleIds) {
            const Profile p = loadExample(QLatin1String(id));
            QVERIFY(p.device.tcp.host.startsWith(QLatin1String("192.0.2.")));
            QCOMPARE(p.device.serial.portName, QStringLiteral("COM1"));
        }
    }

    void HIL_01_scratchRangeRejected_data() {
        QTest::addColumn<QString>("range");
        QTest::addColumn<QString>("message");
        QTest::newRow("bad hex digit") << "W100-W1GG" << "hexadecimal";
        QTest::newRow("hex digit in decimal") << "D100-D2A99" << "decimal";
        QTest::newRow("unknown device type") << "Q100-Q200" << "unknown device type";
        QTest::newRow("ends before it starts") << "D200-D100" << "ends before";
        QTest::newRow("no dash") << "D100" << "FIRST-LAST";
        QTest::newRow("mixed types") << "D100-W200" << "mixes";
        QTest::newRow("empty number") << "D-D5" << "decimal";
    }
    void HIL_01_scratchRangeRejected() {
        QFETCH(QString, range);
        QFETCH(QString, message);
        const QJsonArray scratch{QStringLiteral("D100-D199"), QStringLiteral("M100-M199"), range};
        const ProfileLoad load = loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"),
                                                    QStringLiteral("profile.scratch"), scratch));
        QVERIFY(!load.ok());
        QCOMPARE(load.error.path, QStringLiteral("profile.scratch[2]"));
        QVERIFY2(load.error.message.contains(message), qPrintable(load.error.message));
    }

    // ---- XYN: X/Y octal numbering of an FX profile -------------------------------------

    void HIL_XYN_01_theFxExampleProfilesSetTheNotationTheirManualsUse() {
        const struct {
            const char* id;
            XyNumbering notation;
            XyNumbering digits;
        } expected[] = {{"q03ude-eth-3e-bin", XyNumbering::Hex, XyNumbering::Hex},
                        {"q03ude-c24-3c-f4", XyNumbering::Hex, XyNumbering::Hex},
                        {"fx5u-eth-3e-ascii", XyNumbering::Octal, XyNumbering::Hex},
                        {"fx3-eth-1e-bin", XyNumbering::Octal, XyNumbering::Hex},
                        {"fx3-serial-1c-f1", XyNumbering::Octal, XyNumbering::Octal}};
        for (const auto& e : expected) {
            const Profile p = loadExample(QLatin1String(e.id));
            QVERIFY2(p.device.frame.xyNotation == e.notation, e.id);
            QVERIFY2(p.device.frame.xyAsciiDigits == e.digits, e.id);
        }
        // The FX5U example: Y0-Y7, the outputs whose number is the same in octal and in hex;
        // deviceEnd Y "1777" octal is index 1023.
        const Profile fx5 = loadExample(QStringLiteral("fx5u-eth-3e-ascii"));
        const ScratchRange* y = fx5.firstScratch(DeviceType::Y);
        QVERIFY(y != nullptr);
        QCOMPARE(y->first, 0u);
        QCOMPARE(y->last, 7u);
        QCOMPARE(fx5.end(DeviceType::Y).value_or(0), 1023u);
    }

    void HIL_XYN_02_scratchAndDeviceEndAreReadInTheProfilesNotation() {
        const auto withNotation = [](const QString& notation, const QJsonArray& scratch,
                                     const QJsonObject& deviceEnd) {
            QJsonObject root = edited(QStringLiteral("q03ude-eth-3e-bin"),
                                      QStringLiteral("device.frame.xyNotation"), notation);
            setAt(root, {"profile", "scratch"}, 0, scratch);
            setAt(root, {"profile", "deviceEnd"}, 0, deviceEnd);
            return root;
        };
        const QJsonObject end{{"D", 12287}};
        // Octal: Y0-Y17 is indices 0 to 15, X10-X17 is 8 to 15.
        ProfileLoad oct = loadProfile(withNotation(
            QStringLiteral("Octal"),
            {QStringLiteral("D100-D199"), QStringLiteral("Y0-Y17"), QStringLiteral("X10-17")},
            end));
        QVERIFY2(oct.ok(), qPrintable(oct.error.text()));
        QCOMPARE(oct.profile->scratch[1].first, 0u);
        QCOMPARE(oct.profile->scratch[1].last, 15u);
        QCOMPARE(oct.profile->scratch[2].first, 8u);
        QCOMPARE(oct.profile->scratch[2].last, 15u);
        // The same text under Hex is 0 to 23.
        ProfileLoad hex = loadProfile(withNotation(
            QStringLiteral("Hex"), {QStringLiteral("D100-D199"), QStringLiteral("Y0-Y17")}, end));
        QVERIFY2(hex.ok(), qPrintable(hex.error.text()));
        QCOMPARE(hex.profile->scratch[1].last, 23u);

        // A digit 8 or 9, or a letter, is no octal number; the message says octal.
        for (const char* bad : {"Y0-Y18", "Y8-Y17", "X0-X1F"}) {
            const ProfileLoad load = loadProfile(withNotation(
                QStringLiteral("Octal"), {QStringLiteral("D100-D199"), QLatin1String(bad)}, end));
            QVERIFY2(!load.ok(), bad);
            QCOMPARE(load.error.path, QStringLiteral("profile.scratch[1]"));
            QVERIFY2(load.error.message.contains(QStringLiteral("octal")),
                     qPrintable(load.error.message));
        }

        // deviceEnd: a string in octal, 1777 = index 1023; a letter is refused.
        const ProfileLoad good = loadProfile(withNotation(
            QStringLiteral("Octal"), {QStringLiteral("D100-D199"), QStringLiteral("Y0-Y17")},
            QJsonObject{{"D", 12287}, {"Y", QStringLiteral("1777")}}));
        QVERIFY2(good.ok(), qPrintable(good.error.text()));
        QCOMPARE(good.profile->end(DeviceType::Y).value_or(0), 1023u);
        const ProfileLoad badEnd =
            loadProfile(withNotation(QStringLiteral("Octal"), {QStringLiteral("D100-D199")},
                                     QJsonObject{{"D", 12287}, {"X", QStringLiteral("1FF")}}));
        QVERIFY(!badEnd.ok());
        QCOMPARE(badEnd.error.path, QStringLiteral("profile.deviceEnd.X"));
        QVERIFY(badEnd.error.message.contains(QStringLiteral("octal")));
        // A scratch range may not end beyond deviceEnd: Y0-Y17 is 16 points, "7" ends at index 7.
        const ProfileLoad beyond = loadProfile(withNotation(
            QStringLiteral("Octal"), {QStringLiteral("D100-D199"), QStringLiteral("Y0-Y17")},
            QJsonObject{{"D", 12287}, {"Y", QStringLiteral("7")}}));
        QVERIFY(!beyond.ok());
        QCOMPARE(beyond.error.path, QStringLiteral("profile.scratch[1]"));
    }

    void HIL_XYN_03_deviceTextAndNumbersRoundTripInOctal() {
        QCOMPARE(deviceText(Device{DeviceType::Y, 15}, XyNumbering::Octal), QStringLiteral("Y17"));
        QCOMPARE(deviceText(Device{DeviceType::Y, 15}, XyNumbering::Hex), QStringLiteral("YF"));
        QCOMPARE(deviceText(Device{DeviceType::M, 100}, XyNumbering::Octal),
                 QStringLiteral("M100"));
        QCOMPARE(formatDeviceNumber(DeviceType::X, 255, XyNumbering::Octal), QStringLiteral("377"));
        QCOMPARE(formatDeviceNumber(DeviceType::X, 255, XyNumbering::Hex), QStringLiteral("FF"));
        uint32_t n = 0;
        QVERIFY(parseDeviceNumber(DeviceType::X, QStringLiteral("377"), n, XyNumbering::Octal));
        QCOMPARE(n, 255u);
        QVERIFY(!parseDeviceNumber(DeviceType::X, QStringLiteral("378"), n, XyNumbering::Octal));
        QVERIFY(!parseDeviceNumber(DeviceType::X, QStringLiteral("1F"), n, XyNumbering::Octal));
        QVERIFY(parseDeviceNumber(DeviceType::X, QStringLiteral("1F"), n, XyNumbering::Hex));
        QCOMPARE(n, 31u);
        // Other symbols keep their radix.
        QVERIFY(parseDeviceNumber(DeviceType::D, QStringLiteral("99"), n, XyNumbering::Octal));
        QCOMPARE(n, 99u);
        QVERIFY(!parseDeviceNumber(DeviceType::D, QStringLiteral("9A"), n, XyNumbering::Octal));
        QVERIFY(parseDeviceNumber(DeviceType::W, QStringLiteral("1F"), n, XyNumbering::Octal));
        QCOMPARE(n, 31u);
    }

    void HIL_XYN_04_planReferencesResolveInTheProfilesNotation() {
        // The FX5U example with Y20-Y37 (octal) as its Y scratch.
        const ProfileLoad fxLoad = loadProfile(
            edited(QStringLiteral("fx5u-eth-3e-ascii"), QStringLiteral("profile.scratch"),
                   QJsonArray{QStringLiteral("D100-D2099"), QStringLiteral("M100-M2099"),
                              QStringLiteral("Y20-Y37")}));
        QVERIFY2(fxLoad.ok(), qPrintable(fxLoad.error.text()));
        const Profile fx = *fxLoad.profile;
        const ResolveResult r = resolveText(R"JSON(
            {"id":"Y-01","kind":"read","device":"Y@s","unit":"bit","count":8},
            {"id":"Y-02","kind":"read","device":"Y@s+8","unit":"bit","count":8},
            {"id":"Y-03","kind":"read","device":"Y@s16","unit":"bit","count":16},
            {"id":"Y-04","kind":"read","device":"Y20","unit":"bit","count":1},
            {"id":"Y-05","kind":"read","device":"X377","unit":"bit","count":1},
            {"id":"Y-06","kind":"read","device":"Y@end","unit":"bit","count":1},
            {"id":"Y-07","kind":"read","device":"M100","count":1}
        )JSON",
                                            fx);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        const auto head = [&](int i) { return r.steps[i].ops[0].request.head; };
        QCOMPARE(head(0).number, 16u); // Y20 octal
        QCOMPARE(head(1).number, 24u); // an offset is a decimal count of points: Y30 octal
        QCOMPARE(head(2).number, 16u);
        QCOMPARE(head(3).number, 16u);
        QCOMPARE(head(4).number, 255u);
        QCOMPARE(head(5).number, 1023u); // deviceEnd 1777 octal
        QCOMPARE(head(6).number, 100u);  // M is decimal
        // The descriptions of the steps use the octal text.
        QVERIFY2(r.steps[1].ops[0].description.contains(QStringLiteral("Y30")),
                 qPrintable(r.steps[1].ops[0].description));
        QVERIFY2(r.steps[4].ops[0].description.contains(QStringLiteral("X377")),
                 qPrintable(r.steps[4].ops[0].description));

        // A literal with a digit 8 is no device under octal, and the step names it.
        const ResolveResult bad =
            resolveText(R"JSON({"id":"B-01","kind":"read","device":"X18","unit":"bit"})JSON", fx);
        QVERIFY(!bad.ok());
        QVERIFY2(bad.errors[0].text().contains(QStringLiteral("X18")),
                 qPrintable(bad.errors[0].text()));
        // The same literal is fine under Hex.
        const ResolveResult hex =
            resolveText(R"JSON({"id":"B-01","kind":"read","device":"X18","unit":"bit"})JSON",
                        loadExample(QStringLiteral("q03ude-eth-3e-bin")));
        QVERIFY(hex.ok());
        QCOMPARE(hex.steps[0].ops[0].request.head.number, 0x18u);
    }

    void HIL_01_scratchRangeForms() {
        // Hexadecimal types are read in hexadecimal; the symbol of the second part is optional.
        const QJsonArray scratch{QStringLiteral("W100-W1FF"), QStringLiteral("B20-3F"),
                                 QStringLiteral("d1-d9"), QStringLiteral("Y20-Y2F")};
        const ProfileLoad load = loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"),
                                                    QStringLiteral("profile.scratch"), scratch));
        QVERIFY2(load.ok(), qPrintable(load.error.text()));
        QCOMPARE(load.profile->scratch[0].first, 0x100u);
        QCOMPARE(load.profile->scratch[0].last, 0x1FFu);
        QCOMPARE(load.profile->scratch[1].first, 0x20u);
        QCOMPARE(load.profile->scratch[1].last, 0x3Fu);
        QCOMPARE(load.profile->scratch[2].last, 9u);
        QVERIFY(load.profile->scratch[3].type == DeviceType::Y);
    }

    void HIL_01_deviceEndRules() {
        const auto withEnd = [](const QString& key, const QJsonValue& v) {
            return loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"),
                                      QStringLiteral("profile.deviceEnd.") + key, v));
        };
        QVERIFY(withEnd(QStringLiteral("D"), QStringLiteral("12287")).ok()); // decimal string
        QVERIFY(withEnd(QStringLiteral("W"), QStringLiteral("1fff")).ok());
        ProfileLoad load = withEnd(QStringLiteral("W"), 8191); // hexadecimal type, JSON number
        QVERIFY(!load.ok());
        QCOMPARE(load.error.path, QStringLiteral("profile.deviceEnd.W"));
        load = withEnd(QStringLiteral("W"), QStringLiteral("1FFG"));
        QCOMPARE(load.error.path, QStringLiteral("profile.deviceEnd.W"));
        load = withEnd(QStringLiteral("D"), QStringLiteral("12A"));
        QCOMPARE(load.error.path, QStringLiteral("profile.deviceEnd.D"));
        load = withEnd(QStringLiteral("QQ"), 5);
        QCOMPARE(load.error.path, QStringLiteral("profile.deviceEnd.QQ"));
        QVERIFY(load.error.message.contains(QStringLiteral("unknown device type")));
        // A scratch range cannot end beyond the device.
        load = withEnd(QStringLiteral("D"), 2000);
        QCOMPARE(load.error.path, QStringLiteral("profile.scratch[0]"));
    }

    void HIL_01_unknownDeviceTypeInSupports() {
        const ProfileLoad load = loadProfile(
            edited(QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("profile.supports"),
                   QJsonArray{QStringLiteral("D"), QStringLiteral("Q9")}));
        QVERIFY(!load.ok());
        QCOMPARE(load.error.path, QStringLiteral("profile.supports[1]"));
    }

    // ---- specialFrom: a special range beyond deviceEnd (FX3 D8000-, M8000-) --------------------

    void HIL_01_specialFromLoads() {
        // The FX3 examples declare it; the Q and FX5U examples, whose special devices are SM/SD,
        // do not.
        for (const char* id : {"fx3-eth-1e-bin", "fx3-serial-1c-f1"}) {
            const Profile p = loadExample(QLatin1String(id));
            QCOMPARE(p.special(DeviceType::D).value_or(0), 8000u);
            QCOMPARE(p.special(DeviceType::M).value_or(0), 8000u);
            QVERIFY2(!p.special(DeviceType::R), id);
            QCOMPARE(p.end(DeviceType::D).value_or(0), 7999u);
        }
        for (const char* id : {"q03ude-eth-3e-bin", "q03ude-c24-3c-f4", "fx5u-eth-3e-ascii"}) {
            const Profile p = loadExample(QLatin1String(id));
            for (size_t i = 0; i < static_cast<size_t>(DeviceType::Count); ++i) {
                QVERIFY2(!p.specialFrom[i], id);
            }
        }
        // Absent is fine; a decimal string and a hexadecimal string are read in the type's radix.
        ProfileLoad load = loadProfile(edited(QStringLiteral("fx3-eth-1e-bin"),
                                              QStringLiteral("profile.specialFrom"),
                                              QJsonValue(QJsonValue::Undefined)));
        QVERIFY2(load.ok(), qPrintable(load.error.text()));
        QVERIFY(!load.profile->special(DeviceType::D));
        load = loadProfile(
            edited(QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("profile.specialFrom"),
                   QJsonObject{{"D", QStringLiteral("12288")}, {"W", QStringLiteral("2000")}}));
        QVERIFY2(load.ok(), qPrintable(load.error.text()));
        QCOMPARE(load.profile->special(DeviceType::D).value_or(0), 12288u);
        QCOMPARE(load.profile->special(DeviceType::W).value_or(0), 0x2000u);
    }

    void HIL_01_specialFromRejected_data() {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("message");
        QTest::newRow("not an object") << QJsonValue(8000) << "profile.specialFrom"
                                       << "expected an object";
        QTest::newRow("unknown device type")
            << QJsonValue(QJsonObject{{"QQ", 8000}}) << "profile.specialFrom.QQ"
            << "unknown device type";
        QTest::newRow("equal to deviceEnd")
            << QJsonValue(QJsonObject{{"D", 7999}}) << "profile.specialFrom.D"
            << "must be greater than deviceEnd of D (7999)";
        QTest::newRow("below deviceEnd")
            << QJsonValue(QJsonObject{{"M", 100}}) << "profile.specialFrom.M"
            << "must be greater than deviceEnd of M (7679)";
        QTest::newRow("no deviceEnd of its type")
            << QJsonValue(QJsonObject{{"L", 9000}}) << "profile.specialFrom.L"
            << "needs a deviceEnd of L";
        QTest::newRow("not a decimal number")
            << QJsonValue(QJsonObject{{"D", QStringLiteral("8A00")}}) << "profile.specialFrom.D"
            << "decimal";
        QTest::newRow("negative") << QJsonValue(QJsonObject{{"D", -1}}) << "profile.specialFrom.D"
                                  << "non-negative";
        QTest::newRow("fraction") << QJsonValue(QJsonObject{{"D", 8000.5}})
                                  << "profile.specialFrom.D" << "non-negative";
        QTest::newRow("hexadecimal type as a JSON number")
            << QJsonValue(QJsonObject{{"X", 400}}) << "profile.specialFrom.X" << "hexadecimal";
        QTest::newRow("octal X with a digit 8")
            << QJsonValue(QJsonObject{{"X", QStringLiteral("400")}, {"Y", QStringLiteral("480")}})
            << "profile.specialFrom.Y" << "octal";
        QTest::newRow("neither number nor string")
            << QJsonValue(QJsonObject{{"D", true}}) << "profile.specialFrom.D"
            << "expected a number or a string";
    }
    void HIL_01_specialFromRejected() {
        QFETCH(QJsonValue, value);
        QFETCH(QString, path);
        QFETCH(QString, message);
        const ProfileLoad load = loadProfile(
            edited(QStringLiteral("fx3-eth-1e-bin"), QStringLiteral("profile.specialFrom"), value));
        QVERIFY(!load.ok());
        QCOMPARE(load.error.path, path);
        QVERIFY2(load.error.message.contains(message), qPrintable(load.error.message));
    }

    void HIL_01_specialFromTurnsAPlcErrorReadIntoOkAndSkipsASpanningRead() {
        const Profile fx3 = loadExample(QStringLiteral("fx3-eth-1e-bin")); // D 7999 / 8000
        const ResolveResult r = resolveText(R"JSON(
            {"id":"S-01","kind":"read","device":"D@end+1","expect":"plcError"},
            {"id":"S-02","kind":"read","device":"D@end","count":2,"expect":"plcError"},
            {"id":"S-03","kind":"read","device":"D@end","expect":"ok"},
            {"id":"S-04","kind":"read","device":"M@end+1","unit":"bit","expect":"plcError"},
            {"id":"S-05","kind":"read","device":"M8000","unit":"bit","count":16,
                "expect":"plcError"},
            {"id":"S-06","kind":"read","device":"M7999","unit":"bit","count":2,"expect":"record"},
            {"id":"S-07","kind":"read","device":"D@s","then":[
                {"kind":"read","device":"D7990","count":11,"expect":"plcError"}]},
            {"id":"S-08","kind":"read","device":"D8000","count":3,"expect":"ok"},
            {"id":"S-09","kind":"read","device":"M@end","unit":"word","count":1,
                "expect":"record"},
            {"id":"S-10","kind":"read","device":"D@s","count":5,"expect":"plcError"}
        )JSON",
                                            fx3);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        QCOMPARE(r.steps.size(), 10);
        const auto step = [&](int i) -> const ResolvedStep& { return r.steps[i]; };

        // D8000 exists (a special register): the plcError expectation becomes ok, with a note.
        QVERIFY(!step(0).skipped());
        QVERIFY(step(0).ops[0].expect.kind == ExpectKind::Ok);
        QCOMPARE(step(0).ops[0].expectNote,
                 QStringLiteral("expect ok, not plcError: D8000 is in the special range from "
                                "D8000 (specialFrom)"));
        QCOMPARE(step(0).ops[0].frames.size(), 1); // still encoded and sent
        // D7999 x2 spans the general and the special range: not sent, skipped with the reason.
        QVERIFY(step(1).skipped());
        QCOMPARE(step(1).skipReason,
                 QStringLiteral("skipped: D7999-D8000 spans the general and the special range "
                                "(specialFrom D8000); the PLC forbids reading both in one "
                                "request"));
        // Reads inside the general range keep their expectation.
        QVERIFY(step(2).ops[0].expect.kind == ExpectKind::Ok);
        QVERIFY(step(2).ops[0].expectNote.isEmpty());
        // M7680 lies between deviceEnd and the special range: it does not exist, still plcError.
        QVERIFY(!step(3).skipped());
        QVERIFY(step(3).ops[0].expect.kind == ExpectKind::PlcError);
        QVERIFY(step(3).ops[0].expectNote.isEmpty());
        // A read wholly in the special range, bit device.
        QVERIFY(step(4).ops[0].expect.kind == ExpectKind::Ok);
        QVERIFY(step(4).ops[0].expectNote.contains(QStringLiteral("M8000")));
        // A spanning bit read is skipped whatever it expects.
        QVERIFY2(step(5).skipReason.contains(QStringLiteral("M7999-M8000")),
                 qPrintable(step(5).skipReason));
        // A spanning `then` operation skips its whole step.
        QVERIFY2(step(6).skipReason.contains(QStringLiteral("D7990-D8000")),
                 qPrintable(step(6).skipReason));
        // An ok expectation in the special range is left alone.
        QVERIFY(step(7).ops[0].expect.kind == ExpectKind::Ok);
        QVERIFY(step(7).ops[0].expectNote.isEmpty());
        // A word read of a bit device covers 16 numbers: M7679..M7694 does not reach M8000.
        QVERIFY(!step(8).skipped());
        // In the general range a plcError expectation stays.
        QVERIFY(step(9).ops[0].expect.kind == ExpectKind::PlcError);
        QVERIFY(step(9).ops[0].expectNote.isEmpty());
    }

    void HIL_01_specialFromLeavesWritesToTheSafetyGate() {
        // A write never has its expectation changed or its step skipped by specialFrom: the
        // safety gate judges it (and refuses it, the special range being outside scratch).
        const Profile fx3 = loadExample(QStringLiteral("fx3-eth-1e-bin"));
        const ResolveResult r = resolveText(R"JSON(
            {"id":"W-01","kind":"write","device":"D8000","values":[1],"expect":"plcError"},
            {"id":"W-02","kind":"write","device":"D7999","count":2,"values":[1,2],
                "expect":"plcError"}
        )JSON",
                                            fx3);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        for (const ResolvedStep& s : r.steps) {
            QVERIFY2(!s.skipped(), qPrintable(s.id));
            QVERIFY(s.ops[0].expect.kind == ExpectKind::PlcError);
            QVERIFY(s.ops[0].expectNote.isEmpty());
        }
        const GateReport gate = checkGate(r, fx3);
        QVERIFY(gate.refused());
        QCOMPARE(gate.violations.size(), 2);
    }

    void HIL_01_unknownKeyRejected_data() {
        QTest::addColumn<QString>("parent"); // dotted path of the object that gets a stray key
        QTest::addColumn<QString>("errorPath");
        QTest::newRow("top level") << "" << "typo";
        QTest::newRow("profile") << "profile" << "profile.typo";
        QTest::newRow("deviceEnd is a map, not checked as keys") << "profile.deviceEnd"
                                                                 << "profile.deviceEnd.typo";
        QTest::newRow("specialFrom is a map, like deviceEnd") << "profile.specialFrom"
                                                              << "profile.specialFrom.typo";
        QTest::newRow("device") << "device" << "device.typo";
        QTest::newRow("device.frame") << "device.frame" << "device.frame.typo";
        QTest::newRow("device.session") << "device.session" << "device.session.typo";
        QTest::newRow("device.session.heartbeat") << "device.session.heartbeat"
                                                  << "device.session.heartbeat.typo";
        QTest::newRow("device.transport.tcp") << "device.transport.tcp"
                                              << "device.transport.tcp.typo";
    }
    void HIL_01_unknownKeyRejected() {
        QFETCH(QString, parent);
        QFETCH(QString, errorPath);
        const QString path =
            parent.isEmpty() ? QStringLiteral("typo") : parent + QStringLiteral(".typo");
        const ProfileLoad load =
            loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"), path, QJsonValue(1)));
        QVERIFY(!load.ok());
        QCOMPARE(load.error.path, errorPath);
        // A stray device-end key is "unknown device type"; every other one is "unknown key".
        QVERIFY2(load.error.message.contains(QStringLiteral("unknown")),
                 qPrintable(load.error.message));
    }

    void HIL_01_misspelledKeyDoesNotDropSafetyField() {
        // "scrach" instead of "scratch": the unknown key AND the missing key are both caught, the
        // missing required key first because the loader reads required keys before it finishes.
        QJsonObject root = exampleRoot();
        QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
        profile.insert(QStringLiteral("scrach"), profile.take(QStringLiteral("scratch")));
        root.insert(QStringLiteral("profile"), profile);
        const ProfileLoad load = loadProfile(root);
        QVERIFY(!load.ok());
        QCOMPARE(load.error.path, QStringLiteral("profile.scratch"));
        QVERIFY(load.error.message.contains(QStringLiteral("missing required key")));
    }

    void HIL_01_missingRequiredKeyRejected_data() {
        QTest::addColumn<QString>("path");
        for (const char* p : {"schema", "profile", "device", "profile.id", "profile.plc",
                              "profile.scratch", "profile.deviceEnd", "profile.supports",
                              "profile.specialBit", "profile.specialWord"}) {
            QTest::newRow(p) << QString::fromLatin1(p);
        }
    }
    void HIL_01_missingRequiredKeyRejected() {
        QFETCH(QString, path);
        const ProfileLoad load = loadProfile(
            edited(QStringLiteral("q03ude-eth-3e-bin"), path, QJsonValue(QJsonValue::Undefined)));
        QVERIFY(!load.ok());
        QCOMPARE(load.error.path, path);
        QVERIFY2(load.error.message.contains(QStringLiteral("missing required key")),
                 qPrintable(load.error.message));
    }

    void HIL_01_optionalKeysMayBeAbsent() {
        for (const char* key :
             {"module", "firmware", "adapter", "plcState", "scanTimeDevice", "families"}) {
            const ProfileLoad load =
                loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"),
                                   QStringLiteral("profile.") + QLatin1String(key),
                                   QJsonValue(QJsonValue::Undefined)));
            QVERIFY2(load.ok(), key);
        }
    }

    void HIL_01_otherProfileRulesRejected() {
        ProfileLoad load =
            loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("profile.id"),
                               QStringLiteral("../escape")));
        QCOMPARE(load.error.path, QStringLiteral("profile.id"));
        load = loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("profile.id"),
                                  QStringLiteral("a b")));
        QCOMPARE(load.error.path, QStringLiteral("profile.id"));
        load =
            loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("schema"), 2));
        QCOMPARE(load.error.path, QStringLiteral("schema"));
        load = loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"),
                                  QStringLiteral("profile.specialBit"),
                                  QStringLiteral("SD0"))); // a word device
        QCOMPARE(load.error.path, QStringLiteral("profile.specialBit"));
        load = loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"),
                                  QStringLiteral("profile.specialWord"), QStringLiteral("Z9Z")));
        QCOMPARE(load.error.path, QStringLiteral("profile.specialWord"));
        load = loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"),
                                  QStringLiteral("profile.families"),
                                  QJsonArray{QStringLiteral("a1e"), QStringLiteral("nope")}));
        QCOMPARE(load.error.path, QStringLiteral("profile.families[1]"));
        load = loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"),
                                  QStringLiteral("profile.scanTimeDevice"),
                                  QStringLiteral("nodevice")));
        QCOMPARE(load.error.path, QStringLiteral("profile.scanTimeDevice"));
        // The library's own loader still reports its paths, under "device".
        load =
            loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"),
                               QStringLiteral("device.frame.timeoutMs"), QStringLiteral("soon")));
        QCOMPARE(load.error.path, QStringLiteral("device.frame.timeoutMs"));
        load = loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"),
                                  QStringLiteral("device.transport.tcp.host"), QStringLiteral("")));
        QCOMPARE(load.error.path, QStringLiteral("device.transport.tcp.host"));
    }

    void HIL_01_subscriptionsAndHeartbeatBelongToThePlan() {
        ProfileLoad load = loadProfile(
            edited(QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("device.subscriptions"),
                   QJsonArray{QJsonObject{{QStringLiteral("device"), QStringLiteral("D100")},
                                          {QStringLiteral("count"), 1}}}));
        QVERIFY(!load.ok());
        QCOMPARE(load.error.path, QStringLiteral("device.subscriptions"));

        // A heartbeat writes every round: outside scratch it is refused already by the profile.
        QJsonObject root = exampleRoot();
        QJsonObject device = root.value(QStringLiteral("device")).toObject();
        QJsonObject session = device.value(QStringLiteral("session")).toObject();
        session.insert(QStringLiteral("heartbeat"),
                       QJsonObject{{QStringLiteral("enabled"), true},
                                   {QStringLiteral("device"), QStringLiteral("M5000")}});
        device.insert(QStringLiteral("session"), session);
        root.insert(QStringLiteral("device"), device);
        load = loadProfile(root);
        QVERIFY(!load.ok());
        QCOMPARE(load.error.path, QStringLiteral("device.session.heartbeat.device"));

        session.insert(QStringLiteral("heartbeat"),
                       QJsonObject{{QStringLiteral("enabled"), true},
                                   {QStringLiteral("device"), QStringLiteral("M300")}});
        device.insert(QStringLiteral("session"), session);
        root.insert(QStringLiteral("device"), device);
        QVERIFY(loadProfile(root).ok());
    }

    void HIL_01_unreadableFile() {
        const ProfileLoad load =
            loadProfileFile(testsDir() + QStringLiteral("/hil/profiles/none.json"));
        QVERIFY(!load.ok());
        QVERIFY(load.error.message.contains(QStringLiteral("cannot open")));
    }

    // ---- HIL-01: plans ------------------------------------------------------------------------

    void HIL_01_planWithEveryStepKindLoads() {
        const PlanLoad load = planFrom(R"JSON({
          "schema": 1, "plan": { "id": "p", "title": "t", "family": "qna-ethernet" },
          "steps": [
            { "id": "A-01", "kind": "write", "device": "D@s", "values": [1, "0x2", "3H"],
              "readBack": true },
            { "id": "A-02", "kind": "read", "device": "D@s", "count": "Wmax+40",
              "expect": { "kind": "ok", "frames": 2 } },
            { "id": "A-03", "kind": "mutate", "request": { "kind": "read", "device": "D@s" },
              "edits": [ { "setByte": { "at": 0, "value": "0x51" } }, { "truncate": 1 } ],
              "readOnly": true, "recover": "reconnect",
              "then": [ { "kind": "read", "device": "D@s" } ] },
            { "id": "A-04", "kind": "raw", "hex": "04 0D {EOT}", "readOnly": true },
            { "id": "A-05", "kind": "poll", "heartbeat": "M@s+200", "rounds": 3,
              "subscribe": [ { "name": "d", "device": "D@s", "count": 4 }, { "name": "x",
                  "device": "X0", "count": 32, "input": true } ],
              "actions": [ { "after": 1, "write": { "device": "D@s+1", "values": [7] } },
                           { "after": 2, "subscribe": { "name": "b", "device": "B@s",
                               "count": 16 } },
                           { "after": 2, "unsubscribe": "x" },
                           { "after": 3, "prompt": "pull the cable" } ] },
            { "id": "A-06", "kind": "bench", "pollSet": "A-05", "reps": 5 }
          ] })JSON");
        QVERIFY2(load.ok(), qPrintable(load.error.text()));
        QCOMPARE(load.plan->steps.size(), 6);
        QCOMPARE(load.plan->steps[0].group, QStringLiteral("A"));
        QVERIFY(load.plan->steps[0].op.readBack);
        QVERIFY(load.plan->steps[2].readOnly);
        QVERIFY(load.plan->steps[2].recover == Recover::Reconnect);
        QCOMPARE(load.plan->steps[4].poll.actions.size(), 4);
    }

    void HIL_01_pollActionsRunInTheOrderOfTheirAfter() {
        // Listed out of order; the loader sorts them (stable: the actions of one round keep their
        // written order), so the runner never meets an action whose round has already passed.
        const PlanLoad load = planFrom(R"JSON({
          "schema": 1, "plan": { "id": "p" },
          "steps": [
            { "id": "A-01", "kind": "poll", "rounds": 4,
              "subscribe": [ { "name": "x", "device": "D@s", "count": 2 } ],
              "actions": [ { "after": 3, "prompt": "late" },
                           { "after": 2, "unsubscribe": "x" },
                           { "after": 1, "write": { "device": "D@s+1", "values": [7] } },
                           { "after": 2,
                             "subscribe": { "name": "b", "device": "D@s", "count": 1 } },
                           { "after": 2, "prompt": "second" } ] }
          ] })JSON");
        QVERIFY2(load.ok(), qPrintable(load.error.text()));
        const QVector<PollAction>& actions = load.plan->steps[0].poll.actions;
        QCOMPARE(actions.size(), 5);
        QCOMPARE(actions[0].after, 1);
        QVERIFY(actions[0].kind == PollAction::Kind::Write);
        QCOMPARE(actions[1].after, 2);
        QVERIFY(actions[1].kind == PollAction::Kind::Unsubscribe);
        QCOMPARE(actions[2].after, 2);
        QVERIFY(actions[2].kind == PollAction::Kind::Subscribe);
        QCOMPARE(actions[3].after, 2);
        QVERIFY(actions[3].kind == PollAction::Kind::Prompt);
        QCOMPARE(actions[3].text, QStringLiteral("second"));
        QCOMPARE(actions[4].after, 3);
        QCOMPARE(actions[4].text, QStringLiteral("late"));
    }

    void HIL_01_subscriptionNamesAreCheckedInExecutionOrder() {
        // Listed first, but it runs after the subscription it names: fine.
        const PlanLoad listedFirst = planFrom(R"JSON({
          "schema": 1, "plan": { "id": "p" },
          "steps": [ { "id": "A-01", "kind": "poll", "rounds": 4,
            "subscribe": [ { "name": "d", "device": "D@s", "count": 1 } ],
            "actions": [ { "after": 3, "unsubscribe": "b" },
                         { "after": 1,
                           "subscribe": { "name": "b", "device": "D@s", "count": 1 } } ] }
          ] })JSON");
        QVERIFY2(listedFirst.ok(), qPrintable(listedFirst.error.text()));
        // Listed after, but it runs first: the name is not known yet. The message names the
        // written index of the action.
        const PlanLoad tooEarly = planFrom(R"JSON({
          "schema": 1, "plan": { "id": "p" },
          "steps": [ { "id": "A-01", "kind": "poll", "rounds": 4,
            "subscribe": [ { "name": "d", "device": "D@s", "count": 1 } ],
            "actions": [ { "after": 3, "subscribe": { "name": "b", "device": "D@s", "count": 1 } },
                         { "after": 1, "unsubscribe": "b" } ] }
          ] })JSON");
        QVERIFY(!tooEarly.ok());
        QVERIFY2(tooEarly.error.text().contains(QStringLiteral("steps[0].actions[1]")),
                 qPrintable(tooEarly.error.text()));
        QVERIFY(tooEarly.error.text().contains(QStringLiteral("unknown subscription")));
    }

    void HIL_01_planErrorsNameTheirPath_data() {
        QTest::addColumn<QByteArray>("step");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("message");
        QTest::newRow("unknown step key")
            << QByteArray(R"({"id":"A","kind":"read","device":"D@s","typo":1})") << "steps[0].typo"
            << "unknown key";
        QTest::newRow("missing id")
            << QByteArray(R"({"kind":"read","device":"D@s"})") << "steps[0].id" << "missing";
        QTest::newRow("missing device")
            << QByteArray(R"({"id":"A","kind":"read"})") << "steps[0].device" << "missing";
        QTest::newRow("unknown kind") << QByteArray(R"({"id":"A","kind":"zap","device":"D@s"})")
                                      << "steps[0].kind" << "expected";
        QTest::newRow("bad device") << QByteArray(R"({"id":"A","kind":"read","device":"D@x"})")
                                    << "steps[0].device" << "not a device";
        QTest::newRow("bad count")
            << QByteArray(R"({"id":"A","kind":"read","device":"D@s","count":"Qmax"})")
            << "steps[0].count" << "limit";
        QTest::newRow("write without values")
            << QByteArray(R"({"id":"A","kind":"write","device":"D@s"})") << "steps[0].values"
            << "missing";
        QTest::newRow("bad value")
            << QByteArray(R"({"id":"A","kind":"write","device":"D@s","values":[1,70000]})")
            << "steps[0].values[1]" << "0..65535";
        QTest::newRow("bad bit string")
            << QByteArray(R"({"id":"A","kind":"write","device":"M@s","values":"10x"})")
            << "steps[0].values" << "0 and 1";
        QTest::newRow("bad condition")
            << QByteArray(
                   R"({"id":"A","kind":"read","device":"D@s","requires":["supports:D","wat"]})")
            << "steps[0].requires[1]" << "unknown condition";
        QTest::newRow("bad expect")
            << QByteArray(R"({"id":"A","kind":"read","device":"D@s","expect":"fine"})")
            << "steps[0].expect" << "expected ok";
        QTest::newRow("bad frameOverride key")
            << QByteArray(R"({"id":"A","kind":"read","device":"D@s","frameOverride":{"statio":1}})")
            << "steps[0].frameOverride.statio" << "unknown";
        QTest::newRow("bad edit")
            << QByteArray(
                   R"({"id":"A","kind":"mutate","request":{"kind":"read","device":"D@s"},
                       "edits":[{"poke":1}]})")
            << "steps[0].edits[0].poke" << "unknown edit";
        QTest::newRow("bad hex") << QByteArray(R"({"id":"A","kind":"raw","hex":"0Z"})")
                                 << "steps[0].hex" << "hexadecimal";
        QTest::newRow("bad recover")
            << QByteArray(R"({"id":"A","kind":"raw","hex":"04","recover":"x"})")
            << "steps[0].recover" << "expected";
        QTest::newRow("action after last round")
            << QByteArray(
                   R"({"id":"A","kind":"poll","rounds":2,"subscribe":[{"name":"d","device":"D@s",
                       "count":1}],"actions":[{"after":3,"prompt":"x"}]})")
            << "steps[0].actions[0].after" << "after the last round";
        QTest::newRow("unsubscribe unknown name")
            << QByteArray(
                   R"({"id":"A","kind":"poll","rounds":2,"subscribe":[{"name":"d","device":"D@s",
                       "count":1}],"actions":[{"after":1,"unsubscribe":"zz"}]})")
            << "steps[0].actions[0]" << "unknown subscription";
        QTest::newRow("bench of unknown poll")
            << QByteArray(R"({"id":"A","kind":"bench","pollSet":"nope"})") << "steps[0].pollSet"
            << "no poll step";
        QTest::newRow("bad id") << QByteArray(R"({"id":"A B","kind":"read","device":"D@s"})")
                                << "steps[0].id" << "letters";
    }
    void HIL_01_planErrorsNameTheirPath() {
        QFETCH(QByteArray, step);
        QFETCH(QString, path);
        QFETCH(QString, message);
        const PlanLoad load = loadPlan(QJsonDocument::fromJson(planWith(step)).object());
        QVERIFY(!load.ok());
        QCOMPARE(load.error.path, path);
        QVERIFY2(load.error.message.contains(message), qPrintable(load.error.message));
    }

    void HIL_01_planWholeFileErrors() {
        PlanLoad load = loadPlan(parseJson(R"({"schema":1,"plan":{"id":"t"}})"));
        QCOMPARE(load.error.path, QStringLiteral("steps"));
        load = loadPlan(parseJson(R"({"schema":1,"plan":{"id":"t","x":1},"steps":[]})"));
        QCOMPARE(load.error.path, QStringLiteral("plan.x"));
        load = loadPlan(parseJson(R"({"schema":9,"plan":{"id":"t"},"steps":[]})"));
        QCOMPARE(load.error.path, QStringLiteral("schema"));
        load = loadPlan(QJsonDocument::fromJson(
                            planWith(QByteArray(
                                R"({"id":"A","kind":"read","device":"D@s"},{"id":"A","kind":"read",
                        "device":"D@s"})")))
                            .object());
        QCOMPARE(load.error.path, QStringLiteral("steps[1].id"));
    }

    // ---- device reference resolution (acceptance criterion 2) ---------------------------------

    void HIL_01_everyDeviceReferenceFormResolves() {
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        const ResolveResult r = resolveText(R"JSON(
            {"id":"R-01","kind":"read","device":"D@s"},
            {"id":"R-02","kind":"read","device":"D@s+10"},
            {"id":"R-03","kind":"read","device":"M@s16","count":16},
            {"id":"R-04","kind":"read","device":"D@end"},
            {"id":"R-05","kind":"read","device":"D@end+1"},
            {"id":"R-06","kind":"read","device":"D100","count":3},
            {"id":"R-07","kind":"read","device":"W@s16","count":4},
            {"id":"R-08","kind":"read","device":"W@s16+1"},
            {"id":"R-09","kind":"read","device":"SB","count":16},
            {"id":"R-10","kind":"read","device":"SW+4","count":2},
            {"id":"R-11","kind":"read","device":"M@s16+3","unit":"word"},
            {"id":"R-12","kind":"read","device":"M@s+5","count":2},
            {"id":"R-13","kind":"read","device":"X1F"},
            {"id":"R-14","kind":"read","device":"B@s16"},
            {"id":"R-15","kind":"read","device":"D@end-1"}
        )JSON",
                                            q);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        const auto head = [&](int i) { return r.steps[i].ops[0].request.head; };
        QCOMPARE(deviceText(head(0), XyNumbering::Hex), QStringLiteral("D100"));
        QCOMPARE(deviceText(head(1), XyNumbering::Hex), QStringLiteral("D110"));
        QCOMPARE(deviceText(head(2), XyNumbering::Hex),
                 QStringLiteral("M112")); // 100 aligned up to a multiple of 16
        QCOMPARE(deviceText(head(3), XyNumbering::Hex), QStringLiteral("D12287"));
        QCOMPARE(deviceText(head(4), XyNumbering::Hex),
                 QStringLiteral("D12288")); // first number that does not exist
        QCOMPARE(deviceText(head(5), XyNumbering::Hex), QStringLiteral("D100"));
        QCOMPARE(r.steps[5].ops[0].request.count, uint16_t(3));
        QCOMPARE(deviceText(head(6), XyNumbering::Hex),
                 QStringLiteral("W100")); // hexadecimal: 100H aligned to 16 = 100H
        QCOMPARE(head(6).number, 0x100u);
        QCOMPARE(head(7).number, 0x101u);
        QCOMPARE(deviceText(head(8), XyNumbering::Hex),
                 QStringLiteral("SM0")); // the profile's special bit
        QCOMPARE(deviceText(head(9), XyNumbering::Hex), QStringLiteral("SD4"));
        QCOMPARE(deviceText(head(10), XyNumbering::Hex), QStringLiteral("M115")); // 112 + 3
        QVERIFY(r.steps[10].ops[0].request.op == Op::ReadWords); // unit word on a bit device
        QCOMPARE(deviceText(head(11), XyNumbering::Hex), QStringLiteral("M105"));
        QCOMPARE(deviceText(head(12), XyNumbering::Hex), QStringLiteral("X1F"));
        QCOMPARE(deviceText(head(13), XyNumbering::Hex), QStringLiteral("B100"));
        QCOMPARE(deviceText(head(14), XyNumbering::Hex), QStringLiteral("D12286"));
        QVERIFY(r.steps[0].ops[0].request.scratchRelative);
        QVERIFY(!r.steps[3].ops[0].request.scratchRelative);
        QVERIFY(!r.steps[5].ops[0].request.scratchRelative);
        QVERIFY(r.steps[0].ops[0].notSent.isEmpty());
        QCOMPARE(r.steps[0].ops[0].frames.size(), 1);
    }

    void HIL_01_aScratchRelativeOffsetKeepsItsSignAndIsDecimal() {
        const Profile q =
            loadExample(QStringLiteral("q03ude-eth-3e-bin")); // D100-D2099, W100-W1FF (hex)
        const ResolveResult r = resolveText(R"JSON(
            {"id":"N-01","kind":"read","device":"D@s-1"},
            {"id":"N-02","kind":"read","device":"D@s-100"},
            {"id":"N-03","kind":"read","device":"W@s-1"},
            {"id":"N-04","kind":"read","device":"W@s+10"},
            {"id":"N-05","kind":"read","device":"M@s16-3","unit":"word"}
        )JSON",
                                            q);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        const auto head = [&](int i) { return r.steps[i].ops[0].request.head; };
        QCOMPARE(deviceText(head(0), XyNumbering::Hex),
                 QStringLiteral("D99")); // one below the scratch start
        QCOMPARE(deviceText(head(1), XyNumbering::Hex), QStringLiteral("D0"));
        QCOMPARE(head(2).number, 0xFFu); // W100 is 256: one below is 255 = W0FF
        // "+10" is ten, not 10H: W100 (256) + 10 = 266 = W10A, also on a hexadecimal device.
        QCOMPARE(head(3).number, 266u);
        QCOMPARE(deviceText(head(3), XyNumbering::Hex), QStringLiteral("W10A"));
        QCOMPARE(deviceText(head(4), XyNumbering::Hex), QStringLiteral("M109")); // 112 - 3
        QVERIFY(r.steps[0].ops[0].request.scratchRelative);
    }

    void HIL_01_aReferenceThatResolvesBelowZeroIsAnError() {
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        const ResolveResult r = resolveText(R"JSON(
            {"id":"Z-01","kind":"read","device":"D@s-101"},
            {"id":"Z-02","kind":"read","device":"D@end-20000"},
            {"id":"Z-03","kind":"read","device":"D@s-100"}
        )JSON",
                                            q);
        QVERIFY(!r.ok());
        QStringList named;
        for (const StepError& e : r.errors) {
            named << e.stepId;
            QVERIFY2(e.message.contains(QStringLiteral("outside the device number range")),
                     qPrintable(e.message));
        }
        QCOMPARE(named, (QStringList{"Z-01", "Z-02"})); // D0 is a device; D-1 is not
    }

    void HIL_01_anAlignedReferenceNeedsAnAlignedNumberInsideTheFirstRange() {
        const auto withScratch = [](const QString& range) {
            return *loadProfile(edited(QStringLiteral("q03ude-eth-3e-bin"),
                                       QStringLiteral("profile.scratch"),
                                       QJsonArray{QStringLiteral("D100-D199"), range}))
                        .profile;
        };
        // M100-M111 holds no multiple of 16 (112 is the next one); M100-M112 does.
        ResolveResult r =
            resolveText(R"({"id":"A-01","kind":"read","device":"M@s16","unit":"word"})",
                        withScratch(QStringLiteral("M100-M111")));
        QVERIFY(!r.ok());
        QCOMPARE(r.errors[0].stepId, QStringLiteral("A-01"));
        QVERIFY2(r.errors[0].message.contains(QStringLiteral("no number aligned to 16")),
                 qPrintable(r.errors[0].message));
        r = resolveText(R"({"id":"A-01","kind":"read","device":"M@s16","unit":"word"})",
                        withScratch(QStringLiteral("M100-M112")));
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        QCOMPARE(deviceText(r.steps[0].ops[0].request.head, XyNumbering::Hex),
                 QStringLiteral("M112"));
        // A multiple of 16 that is the very first number needs no rounding.
        r = resolveText(R"({"id":"A-02","kind":"read","device":"M@s16","unit":"word"})",
                        withScratch(QStringLiteral("M96-M111")));
        QVERIFY(r.ok());
        QCOMPARE(deviceText(r.steps[0].ops[0].request.head, XyNumbering::Hex),
                 QStringLiteral("M96"));
    }

    void HIL_01_aMinimumOfTwoLimitsIsTheSmallerOne() {
        // 1C Format 1 reads at most 256 bit points and writes at most 160: the smaller is 160
        // whichever way round the limits are named. 3E Binary has 7168 for both.
        const Profile c1 = loadExample(QStringLiteral("fx3-serial-1c-f1"));
        const ResolveResult r = resolveText(R"JSON(
            {"id":"M-01","kind":"read","device":"M@s","count":"BRmax"},
            {"id":"M-02","kind":"read","device":"M@s","count":"BWmax"},
            {"id":"M-03","kind":"read","device":"M@s","count":"min(BRmax,BWmax)"},
            {"id":"M-04","kind":"read","device":"M@s","count":"min(BWmax,BRmax)"}
        )JSON",
                                            c1);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        QCOMPARE(r.steps[0].ops[0].request.count, uint16_t(256));
        QCOMPARE(r.steps[1].ops[0].request.count, uint16_t(160));
        QCOMPARE(r.steps[2].ops[0].request.count, uint16_t(160));
        QCOMPARE(r.steps[3].ops[0].request.count, uint16_t(160));
    }

    void HIL_01_referenceErrorsNameTheStep() {
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        const ResolveResult r = resolveText(R"JSON(
            {"id":"E-01","kind":"read","device":"D@s"},
            {"id":"E-02","kind":"write","device":"ZR@s","values":[1]},
            {"id":"E-03","kind":"read","device":"@scan"},
            {"id":"E-04","kind":"read","device":"STN@end"}
        )JSON",
                                            q);
        QVERIFY(!r.ok());
        QStringList named;
        for (const StepError& e : r.errors) {
            named << e.stepId;
        }
        QVERIFY2(named.contains(QStringLiteral("E-02")), qPrintable(named.join(',')));
        QVERIFY(named.contains(QStringLiteral("E-03")));
        QVERIFY(named.contains(QStringLiteral("E-04")));
        QVERIFY(!named.contains(QStringLiteral("E-01")));
        for (const StepError& e : r.errors) {
            if (e.stepId == QLatin1String("E-02")) {
                QVERIFY2(e.message.contains(QStringLiteral("scratch range of ZR")),
                         qPrintable(e.message));
                QVERIFY(e.text().startsWith(QStringLiteral("E-02: ")));
            }
        }
    }

    void HIL_01_countsValuesAndLimits() {
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        const ResolveResult r = resolveText(R"JSON(
            {"id":"C-01","kind":"read","device":"D@s","count":"Wmax"},
            {"id":"C-02","kind":"read","device":"D@s","count":"Wmax+40"},
            {"id":"C-03","kind":"read","device":"M@s","count":"min(BRmax,BWmax)"},
            {"id":"C-04","kind":"read","device":"M@s16","unit":"word","count":"WBRmax"},
            {"id":"C-05","kind":"write","device":"D@s","values":[1,"0x10","20H"]},
            {"id":"C-06","kind":"write","device":"D@s+20","values":[{"f64":1.5},{"i32":-2}]},
            {"id":"C-07","kind":"write","device":"M@s","values":"1100"},
            {"id":"C-08","kind":"write","device":"D@s","count":4,"values":{"gen":"index",
                "mul":257}},
            {"id":"C-09","kind":"write","device":"M@s","count":5,"values":{"gen":"alt","first":1}},
            {"id":"C-10","kind":"write","device":"D@s","count":2,"values":{"gen":"fill",
                "value":65535}},
            {"id":"C-11","kind":"write","device":"D@s","values":[1,2],"readBack":true}
        )JSON",
                                            q);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        QCOMPARE(r.steps[0].ops[0].request.count, uint16_t(960)); // 3E Binary Wmax
        QCOMPARE(r.steps[1].ops[0].request.count, uint16_t(1000));
        QCOMPARE(r.steps[1].ops[0].frames.size(), 2);              // read split into two commands
        QCOMPARE(r.steps[2].ops[0].request.count, uint16_t(7168)); // min(BRmax, BWmax), 3E Binary
        QCOMPARE(r.steps[3].ops[0].request.count, uint16_t(960));
        QCOMPARE(toBytes(r.steps[4].ops[0].request.data), QByteArray::fromHex("010010002000"));
        QCOMPARE(toBytes(r.steps[5].ops[0].request.data),
                 toBytes(convert::fromFloat64({1.5})) + toBytes(convert::fromInt32({-2})));
        QCOMPARE(r.steps[5].ops[0].request.count, uint16_t(6));
        QVERIFY(r.steps[6].ops[0].request.op == Op::WriteBits);
        QCOMPARE(toBytes(r.steps[6].ops[0].request.data), QByteArray::fromHex("01010000"));
        QCOMPARE(toBytes(r.steps[7].ops[0].request.data), QByteArray::fromHex("0000010102020303"));
        QCOMPARE(toBytes(r.steps[8].ops[0].request.data), QByteArray::fromHex("0100010001"));
        QCOMPARE(toBytes(r.steps[9].ops[0].request.data), QByteArray::fromHex("ffffffff"));
        QCOMPARE(r.steps[10].ops.size(), 2);
        QVERIFY(r.steps[10].ops[1].readBack);
        QCOMPARE(r.steps[10].ops[1].recordId, QStringLiteral("C-11.2"));
        QCOMPARE(r.steps[10].ops[1].expect.values, (QVector<uint16_t>{1, 2}));
        QVERIFY(r.steps[10].ops[1].request.op == Op::ReadWords);
        QCOMPARE(r.steps[10].ops[1].request.count,
                 uint16_t(2)); // the read-back covers what was written
    }

    void HIL_01_limitStepsSkipWhenScratchIsTooSmall() {
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        const ResolveResult r = resolveText(R"JSON(
            {"id":"L-01","kind":"write","device":"M@s16","unit":"word","count":"WBWmax",
             "values":{"gen":"index","mul":1},"scratchTooSmall":"skip"},
            {"id":"L-02","kind":"write","device":"M@s16","unit":"word","count":"WBWmax",
             "values":{"gen":"index","mul":1}},
            {"id":"L-03","kind":"write","device":"D@s","count":"Wmax","values":{"gen":"index",
                "mul":1},"scratchTooSmall":"skip"}
        )JSON",
                                            q);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        QVERIFY(r.steps[0].skipped());
        QVERIFY(r.steps[0].skipReason.contains(QStringLiteral("scratch too small")));
        QVERIFY(!r.steps[1].skipped()); // without the key the gate refuses it
        QVERIFY(r.steps[1].ops[0].scratchMisfit);
        QVERIFY(!r.steps[2].skipped()); // 960 words fit D100-D2099
    }

    void HIL_01_foreachAndConditions() {
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        ResolveResult r = resolveText(R"JSON(
            {"id":"F-01","kind":"read","device":"{T}0","foreach":"supports"},
            {"id":"F-02","kind":"read","device":"{T}0","foreach":"other"}
        )JSON",
                                      q);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        int supports = 0;
        int others = 0;
        for (const ResolvedStep& s : r.steps) {
            if (s.id.startsWith(QStringLiteral("F-01-"))) {
                ++supports;
                // bit types read bits, word types read words
                const bool isBit = deviceInfo(s.ops[0].request.head.type).kind == DeviceKind::Bit;
                QCOMPARE(s.ops[0].request.op == Op::ReadBits, isBit);
            } else {
                ++others;
                const std::optional<DeviceType> t = deviceTypeFromSymbol(s.id.mid(5));
                QVERIFY(t && !q.supportsType(*t));
            }
        }
        QCOMPARE(supports, q.supports.size());
        QVERIFY(others > 0);
        QCOMPARE(r.steps[0].id, QStringLiteral("F-01-D"));

        r = resolveText(R"JSON(
            {"id":"K-01","kind":"read","device":"D@s","requires":["supports:D","scratch:Y",
                "frame:3E","code:Binary"]},
            {"id":"K-02","kind":"read","device":"D@s","requires":["!supports:D"]},
            {"id":"K-03","kind":"read","device":"@scan","requires":["scanTime"]},
            {"id":"K-04","kind":"read","device":"D@s","requires":["format:4"]},
            {"id":"K-05","kind":"read","device":"M@end","requires":["endNotMultiple16:M"]},
            {"id":"K-06","kind":"read","device":"D@s","requires":["profile:q03ude-eth-3e-bin",
                "sumCheck:on"]},
            {"id":"K-07","kind":"read","device":"D@s","requires":["profile:other"]}
        )JSON",
                        q);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        QVERIFY(!r.steps[0].skipped());
        QVERIFY(r.steps[1].skipped());
        QVERIFY(r.steps[1].skipReason.contains(QStringLiteral("!supports:D")));
        QVERIFY(r.steps[2]
                    .skipped()); // the example has no scanTimeDevice, so '@scan' is never resolved
        QVERIFY(r.steps[3].skipped());
        QVERIFY(r.steps[4].skipped()); // 8191 + 1 = 8192, a multiple of 16
        QVERIFY(!r.steps[5].skipped());
        QVERIFY(r.steps[6].skipped());
    }

    void HIL_01_onlyKeepsTheNamedGroups() {
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        const ResolveResult r =
            resolveText(R"JSON(
            {"id":"G1-01","kind":"read","device":"D@s"},
            {"id":"G2-01","kind":"read","device":"D@s"},
            {"id":"G8-Q1","kind":"read","device":"D@s"}
        )JSON",
                        q, QStringList{QStringLiteral("g1"), QStringLiteral("G8")});
        QCOMPARE(r.steps.size(), 2);
        QCOMPARE(r.steps[1].group, QStringLiteral("G8"));
    }

    void HIL_01_mutateEditsMatchAnIndependentEncode() {
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        const FrameConfig frame = q.device.frame;
        const McProtocol codec(frame);
        const Request read = Request::readWords(Device{DeviceType::D, 100}, 1);
        const ByteBuf base = codec.encode(read).value();
        const ResolveResult r = resolveText(R"JSON(
            {"id":"M-01","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"setByte":{"at":0,"value":"0x51"}}],"readOnly":true},
            {"id":"M-02","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"truncate":1}],"readOnly":true},
            {"id":"M-03","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"append":"30"}]},
            {"id":"M-04","kind":"mutate","request":{"kind":"write","device":"M@s","values":"10101"},
             "edits":[{"setNibble":{"at":-1,"nibble":"low","value":1}}]},
            {"id":"M-05","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"setByte":{"at":-1,"value":"0xFF"}},{"setNibble":{"at":0,"nibble":"high",
                "value":15}}]},
            {"id":"M-06","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"setByte":{"at":99,"value":1}}]},
            {"id":"M-07","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"replaceSum":{"add":1}}]}
        )JSON",
                                            q);
        // M-06 (position outside the frame) and M-07 (no SUM on an Ethernet frame) are errors.
        QStringList failed;
        for (const StepError& e : r.errors) {
            failed << e.stepId;
        }
        QCOMPARE(failed, (QStringList{QStringLiteral("M-06"), QStringLiteral("M-07")}));
        ByteBuf expect = base;
        expect[0] = 0x51;
        QCOMPARE(r.steps[0].ops[0].frames[0], expect);
        QVERIFY(r.steps[0].ops[0].readOnly);
        QVERIFY(r.steps[0].ops[0].via == Via::Mutate);
        expect = base;
        expect.pop_back();
        QCOMPARE(r.steps[1].ops[0].frames[0], expect);
        expect = base;
        expect.push_back(0x30);
        QCOMPARE(r.steps[2].ops[0].frames[0], expect);
        // 3E Binary odd bit write: M100 x5 = 1,0,1,0,1 -> data 10 10 1(pad): the padding nibble is
        // the last low nibble.
        const std::vector<uint8_t> bits = {1, 0, 1, 0, 1};
        Request w =
            Request::writeBits(Device{DeviceType::M, 100}, ByteView{bits.data(), bits.size()});
        expect = codec.encode(w).value();
        QCOMPARE(expect.back(), uint8_t(0x10));
        expect.back() = 0x11;
        QCOMPARE(r.steps[3].ops[0].frames[0], expect);
        expect = base;
        expect.back() = 0xFF;
        expect[0] = static_cast<uint8_t>((expect[0] & 0x0F) | 0xF0);
        QCOMPARE(r.steps[4].ops[0].frames[0], expect);
    }

    void HIL_01_serialMutatesAndRaw() {
        const Profile c24 = loadExample(QStringLiteral("q03ude-c24-3c-f4"));
        const McProtocol codec(c24.device.frame);
        const ByteBuf base =
            codec.encode(Request::readWords(Device{DeviceType::D, 100}, 1)).value();
        const ResolveResult r = resolveText(R"JSON(
            {"id":"S-01","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"replaceSum":{"add":1}}]},
            {"id":"S-02","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"replaceSum":{"value":"0x00"}}]},
            {"id":"S-03","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "frameOverride":{"stationNo":"+1"}},
            {"id":"S-04","kind":"raw","hex":"{EOT}","then":[{"kind":"read","device":"D@s"}]},
            {"id":"S-05","kind":"raw","hex":"04 0D 0A 05"}
        )JSON",
                                            c24);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        // Format 4 ends with SUM CR LF; the SUM is two upper-case hex characters.
        const size_t at = base.size() - 4;
        const auto digit = [](uint8_t c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
        const int sum = digit(base[at]) * 16 + digit(base[at + 1]);
        QCOMPARE(r.steps[0].ops[0].frames[0].size(), base.size());
        const ByteBuf plusOne = r.steps[0].ops[0].frames[0];
        QCOMPARE(digit(plusOne[at]) * 16 + digit(plusOne[at + 1]), (sum + 1) & 0xFF);
        QCOMPARE(plusOne[at + 2], base[at + 2]); // CR LF untouched
        QCOMPARE(plusOne[0], base[0]);
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(&r.steps[1].ops[0].frames[0][at]), 2),
                 QByteArray("00"));
        FrameConfig next = c24.device.frame;
        next.stationNo = static_cast<uint8_t>(next.stationNo + 1);
        QCOMPARE(r.steps[2].frame.stationNo, next.stationNo);
        QCOMPARE(
            r.steps[2].ops[0].frames[0],
            McProtocol(next).encode(Request::readWords(Device{DeviceType::D, 100}, 1)).value());
        QVERIFY(r.steps[2].ops[0].frames[0] != base);
        QCOMPARE(r.steps[3].ops[0].frames[0], (ByteBuf{0x04, 0x0D, 0x0A})); // format 4: EOT CR LF
        QCOMPARE(r.steps[3].ops.size(), 2);
        QCOMPARE(r.steps[4].ops[0].frames[0], (ByteBuf{0x04, 0x0D, 0x0A, 0x05}));

        QJsonObject root = exampleRoot(QStringLiteral("q03ude-c24-3c-f4"));
        QJsonObject device = root.value(QStringLiteral("device")).toObject();
        QJsonObject frame = device.value(QStringLiteral("frame")).toObject();
        frame.insert(QStringLiteral("format"), QStringLiteral("Format1"));
        device.insert(QStringLiteral("frame"), frame);
        root.insert(QStringLiteral("device"), device);
        const Profile f1 = *loadProfile(root).profile;
        const ResolveResult r2 =
            resolveText(R"JSON({"id":"S-06","kind":"raw","hex":"{EOT}"})JSON", f1);
        QVERIFY(r2.ok());
        QCOMPARE(r2.steps[0].ops[0].frames[0], (ByteBuf{0x04}));
        const ResolveResult r3 = resolveText(R"JSON({"id":"S-07","kind":"raw","hex":"{EOT}"})JSON",
                                             loadExample(QStringLiteral("q03ude-eth-3e-bin")));
        QVERIFY(!r3.ok());
    }

    void HIL_01_frameOverrideErrors() {
        const Profile c24 = loadExample(QStringLiteral("q03ude-c24-3c-f4"));
        const ResolveResult r = resolveText(R"JSON(
            {"id":"O-01","kind":"read","device":"D@s","frameOverride":{"stationNo":"+1",
                "code":"Binary"}},
            {"id":"O-02","kind":"read","device":"D@s","frameOverride":{"code":"Hex"}},
            {"id":"O-03","kind":"read","device":"D@s","frameOverride":{"code":"+1"}}
        )JSON",
                                            c24);
        QCOMPARE(r.errors.size(), 3); // 3C must stay ASCII; "Hex" is no code; "+1" needs a number
        for (const StepError& e : r.errors) {
            QVERIFY2(e.message.contains(QStringLiteral("frameOverride")), qPrintable(e.message));
        }
    }

    void HIL_01_mirrorsPlaceholders() {
        const ResolveResult bin = resolveText(
            R"JSON({"id":"V-01","kind":"read","device":"D100","mirrors":"V-3E-{x}-01"})JSON",
            loadExample(QStringLiteral("q03ude-eth-3e-bin")));
        QCOMPARE(bin.steps[0].ops[0].mirrors, QStringLiteral("V-3E-B-01"));
        const ResolveResult ser = resolveText(
            R"JSON({"id":"V-01","kind":"read","device":"D100","mirrors":"V-3C{n}-01"})JSON",
            loadExample(QStringLiteral("q03ude-c24-3c-f4")));
        QCOMPARE(ser.steps[0].ops[0].mirrors, QStringLiteral("V-3C4-01"));
    }

    void HIL_01_thenOperationsRawRequestsAndPollOptions() {
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        const McProtocol codec(q.device.frame);
        const ResolveResult r = resolveText(R"JSON(
            {"id":"T-01","kind":"write","device":"M@s","values":"1","readBack":true,
             "then":[{"kind":"write","device":"M@s","values":"0","readBack":true}]},
            {"id":"T-02","kind":"raw","requests":[{"kind":"read","device":"D@s"},{"kind":"read",
                "device":"D@s+10"}]},
            {"id":"T-03","kind":"poll","rounds":1,"bitsAsWords":false,"subscribe":[{"name":"a",
                "device":"M@s","count":8}]}
        )JSON",
                                            q);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        QCOMPARE(r.steps[0].ops.size(), 4); // write, read back, write, read back
        QCOMPARE(r.steps[0].ops[3].recordId, QStringLiteral("T-01.4"));
        QVERIFY(r.steps[0].ops[3].readBack);
        QCOMPARE(r.steps[0].ops[3].expect.values, (QVector<uint16_t>{0}));
        QCOMPARE(r.steps[0].ops[1].expect.values, (QVector<uint16_t>{1}));
        // A raw step of requests sends the encoded frames back to back in one write.
        ByteBuf both = codec.encode(Request::readWords(Device{DeviceType::D, 100}, 1)).value();
        const ByteBuf second =
            codec.encode(Request::readWords(Device{DeviceType::D, 110}, 1)).value();
        both.insert(both.end(), second.begin(), second.end());
        QCOMPARE(r.steps[1].ops[0].frames.size(), 1);
        QCOMPARE(r.steps[1].ops[0].frames[0], both);
        QVERIFY(r.steps[2].poll.bitsAsWords.has_value() && !*r.steps[2].poll.bitsAsWords);
        QCOMPARE(r.steps[2].poll.subs[0].count, 8u);
    }

    // ---- the command line (acceptance criterion 3) --------------------------------------------

    void HIL_01_helpListsTheOptionsOfTheSpec() {
        const ParseResult p =
            parseOptions({QStringLiteral("hil_capture"), QStringLiteral("--help")});
        QVERIFY(p.status == CommandLineStatus::Help);
        for (const char* opt :
             {"--profile", "--plan", "--dry-run", "--only", "--yes", "--bench-reps", "--plc-state",
              "--report", "--output-root", "--note", "--reconnect-timeout"}) {
            QVERIFY2(p.text.contains(QLatin1String(opt)), opt);
        }
        QVERIFY(p.text.contains(QStringLiteral("Exit codes")));
        QVERIFY(p.text.contains(QStringLiteral("safety gate")));
    }

    void HIL_01_commandLineParsing() {
        ParseResult p = parseOptions(
            {QStringLiteral("x"), QStringLiteral("--profile"), QStringLiteral("a.json"),
             QStringLiteral("--plan"), QStringLiteral("b.json"), QStringLiteral("--dry-run"),
             QStringLiteral("--yes"), QStringLiteral("--only"), QStringLiteral("G1, G2,G8"),
             QStringLiteral("--bench-reps"), QStringLiteral("7"), QStringLiteral("--plc-state"),
             QStringLiteral("stop")});
        QVERIFY(p.status == CommandLineStatus::Run);
        QVERIFY(p.options.dryRun && p.options.yes);
        QCOMPARE(p.options.only,
                 (QStringList{QStringLiteral("G1"), QStringLiteral("G2"), QStringLiteral("G8")}));
        QCOMPARE(p.options.benchReps, 7);
        QCOMPARE(p.options.plcState, QStringLiteral("STOP"));
        QCOMPARE(p.options.outputRoot, QStringLiteral("tests/vectors/captured"));

        p = parseOptions({QStringLiteral("x"), QStringLiteral("--report")});
        QVERIFY(p.status == CommandLineStatus::Run && p.options.report);
        QVERIFY(parseOptions({QStringLiteral("x")}).status == CommandLineStatus::Error);
        QVERIFY(
            parseOptions({QStringLiteral("x"), QStringLiteral("--profile"), QStringLiteral("a")})
                .status == CommandLineStatus::Error);
        QVERIFY(parseOptions({QStringLiteral("x"), QStringLiteral("--nope")}).status ==
                CommandLineStatus::Error);
        QVERIFY(parseOptions({QStringLiteral("x"), QStringLiteral("--report"),
                              QStringLiteral("--plc-state"), QStringLiteral("HALT")})
                    .status == CommandLineStatus::Error);
        QVERIFY(parseOptions({QStringLiteral("x"), QStringLiteral("--report"),
                              QStringLiteral("--bench-reps"), QStringLiteral("0")})
                    .status == CommandLineStatus::Error);
    }
};

} // namespace

QObject* makeProfileSuite() { return new HilProfileTests; }

} // namespace mc::hil::test

#include "tst_hil_profile.moc"
