// The four plans (tests/hil/plans) are the machine form of docs/hil/COMMAND-CATALOGUE.md.
// Every catalogue id is covered by the plans of the families whose column says so, no plan has an
// id the catalogue lacks, and every plan passes --dry-run (safety gate included) against the
// example profiles. No hardware.
#include "hil_suites.h"
#include "hil_test_support.h"

#include "hil_capture/dry_run.h"
#include "hil_capture/options.h"
#include "hil_capture/plan.h"
#include "hil_capture/profile.h"
#include "hil_capture/resolve.h"
#include "hil_capture/tool.h"

#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QtTest>

namespace mc::hil::test {

namespace {

const char* const kFamilies[] = {"3E", "1E", "3C", "1C"};
const char* const kPlanFiles[] = {"qna_ethernet", "a1e", "qna_serial", "a1c"};

// Catalogue ids the v1 plans leave out: G9-02, G9-03 and G9-04 write through frames MockPlc does
// not decode, so the safety gate cannot prove the writes land in scratch. See the note under the G9
// table of docs/hil/COMMAND-CATALOGUE.md (owner decision 2026-10-02).
const QSet<QString> kNotInV1Plans{QStringLiteral("G9-02"), QStringLiteral("G9-03"),
                                  QStringLiteral("G9-04")};

struct CatalogueRow {
    QString id;
    bool columns{false}; ///< the row has the four family columns
    bool family[4]{};    ///< runs for 3E, 1E, 3C, 1C
};

QVector<CatalogueRow> readCatalogue() {
    QFile f(testsDir() + QStringLiteral("/../docs/hil/COMMAND-CATALOGUE.md"));
    QVector<CatalogueRow> rows;
    if (!f.open(QIODevice::ReadOnly)) {
        return rows;
    }
    static const QRegularExpression idRe(QStringLiteral(R"(^G[0-9VB]+-[A-Za-z0-9]+$)"));
    static const QRegularExpression headRe(QStringLiteral(R"(^## (G[0-9]+) )"));
    for (const QString& line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch head = headRe.match(line);
        if (head.hasMatch() && head.captured(1) == QLatin1String("G6")) {
            rows.push_back(
                CatalogueRow{head.captured(1), false, {}}); // G6 is one poll step without ids
        }
        if (!line.startsWith(QLatin1Char('|'))) {
            continue;
        }
        const QStringList cells = line.split(QLatin1Char('|'));
        if (cells.size() < 3 || !idRe.match(cells[1].trimmed()).hasMatch()) {
            continue;
        }
        CatalogueRow row;
        row.id = cells[1].trimmed();
        if (cells.size() >= 8) {
            bool all = true;
            bool any = false;
            for (int i = 0; i < 4; ++i) {
                const QString c = cells[3 + i].trimmed();
                const bool yes = c.startsWith(QStringLiteral("✓")) || c == QLatin1String("notSent");
                const bool no = c == QStringLiteral("—");
                all = all && (yes || no);
                any = any || yes;
                row.family[i] = yes;
            }
            row.columns = all && any;
        }
        rows.push_back(row);
    }
    return rows;
}

bool belongsTo(const QString& planId, const QString& catalogueId) {
    return planId == catalogueId || planId.startsWith(catalogueId + QLatin1Char('-'));
}

PlanLoad loadPlanNamed(const char* file) {
    return loadPlanFile(testsDir() + QStringLiteral("/hil/plans/") + QLatin1String(file) +
                        QStringLiteral(".json"));
}

const ResolvedStep* findStep(const ResolveResult& r, const QString& id) {
    for (const ResolvedStep& s : r.steps) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

class HilPlansTests : public QObject {
    Q_OBJECT

  private slots:
    void HIL_01_catalogueIsReadable() {
        const QVector<CatalogueRow> rows = readCatalogue();
        // 74 catalogue ids plus the G6 poll; a changed catalogue makes this fail on purpose.
        QCOMPARE(rows.size(), 75);
    }

    void HIL_01_everyCatalogueIdIsInThePlansOfItsFamilies() {
        const QVector<CatalogueRow> catalogue = readCatalogue();
        QVERIFY(!catalogue.isEmpty());
        QVector<PlanLoad> plans;
        for (const char* file : kPlanFiles) {
            plans.push_back(loadPlanNamed(file));
            QVERIFY2(plans.last().ok(), qPrintable(QLatin1String(file) + QStringLiteral(": ") +
                                                   plans.last().error.text()));
        }
        QSet<QString> covered;
        for (const CatalogueRow& row : catalogue) {
            if (kNotInV1Plans.contains(row.id)) {
                for (const PlanLoad& p : plans) {
                    for (const Step& s : p.plan->steps) {
                        QVERIFY2(!belongsTo(s.id, row.id),
                                 qPrintable(row.id + QStringLiteral(" is not a v1 step")));
                    }
                }
                continue;
            }
            int inPlans = 0;
            for (int f = 0; f < 4; ++f) {
                int count = 0;
                for (const Step& s : plans[f].plan->steps) {
                    count += belongsTo(s.id, row.id) ? 1 : 0;
                }
                inPlans += count > 0 ? 1 : 0;
                if (row.columns) {
                    QVERIFY2((count > 0) == row.family[f],
                             qPrintable(QStringLiteral(
                                            "%1 in the %2 plan: %3 step(s), the catalogue says %4")
                                            .arg(row.id, QLatin1String(kFamilies[f]))
                                            .arg(count)
                                            .arg(row.family[f])));
                }
            }
            QVERIFY2(inPlans >= 1, qPrintable(row.id + QStringLiteral(" is in no plan")));
            covered.insert(row.id);
        }
        // No plan has an id the catalogue lacks.
        for (int f = 0; f < 4; ++f) {
            for (const Step& s : plans[f].plan->steps) {
                bool known = false;
                for (const CatalogueRow& row : catalogue) {
                    known = known || (belongsTo(s.id, row.id) && !kNotInV1Plans.contains(row.id));
                }
                QVERIFY2(known, qPrintable(QLatin1String(kFamilies[f]) +
                                           QStringLiteral(" plan: unknown step id ") + s.id));
            }
        }
        QVERIFY(covered.contains(QStringLiteral("G8-Q6")) &&
                covered.contains(QStringLiteral("GB-11")) &&
                covered.contains(QStringLiteral("G6")));
    }

    void HIL_01_everyPlanPassesDryRunAgainstItsExampleProfiles_data() {
        QTest::addColumn<QString>("plan");
        QTest::addColumn<QString>("profile");
        QTest::newRow("3E binary") << "qna_ethernet" << "q03ude-eth-3e-bin";
        QTest::newRow("3E ascii") << "qna_ethernet" << "fx5u-eth-3e-ascii";
        QTest::newRow("1E binary") << "a1e" << "fx3-eth-1e-bin";
        QTest::newRow("3C format 4") << "qna_serial" << "q03ude-c24-3c-f4";
        QTest::newRow("1C format 1") << "a1c" << "fx3-serial-1c-f1";
    }
    void HIL_01_everyPlanPassesDryRunAgainstItsExampleProfiles() {
        QFETCH(QString, plan);
        QFETCH(QString, profile);
        Options options;
        options.profilePath = exampleProfilePath(profile);
        options.planPath =
            testsDir() + QStringLiteral("/hil/plans/") + plan + QStringLiteral(".json");
        options.dryRun = true;
        const ToolRun run = runToolWith(options);
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.err + run.out.left(2000)));
        QVERIFY(run.out.contains(QStringLiteral("safety gate OK")));
        QVERIFY2(run.out.count(QStringLiteral("    tx ")) > 40,
                 qPrintable(QString::number(run.out.count(QStringLiteral("    tx ")))));
        // Steps that do not apply to the profile are skipped, not errors.
        QVERIFY(run.out.contains(QStringLiteral("skipped: requires")));
    }

    // specialFrom (SPEC-hil-capture "Profile"; catalogue G5-02, G5-03): on an FX3 profile D8000 is
    // a special register, so G5-02 expects ok, and G5-03 (D7999 x2) is not sent.
    void HIL_01_theFx3BoundaryStepsFollowSpecialFrom_data() {
        QTest::addColumn<QString>("plan");
        QTest::addColumn<QString>("profile");
        QTest::newRow("1E") << "a1e" << "fx3-eth-1e-bin";
        QTest::newRow("1C") << "a1c" << "fx3-serial-1c-f1";
    }
    void HIL_01_theFx3BoundaryStepsFollowSpecialFrom() {
        QFETCH(QString, plan);
        QFETCH(QString, profile);
        const PlanLoad p = loadPlanNamed(qPrintable(plan));
        QVERIFY(p.ok());
        const Profile fx3 = loadExample(profile);
        QVERIFY(fx3.special(DeviceType::D).has_value());
        const ResolveResult r = resolvePlan(*p.plan, fx3);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        const ResolvedStep* g502 = findStep(r, QStringLiteral("G5-02"));
        const ResolvedStep* g503 = findStep(r, QStringLiteral("G5-03"));
        QVERIFY(g502 != nullptr && g503 != nullptr);
        QVERIFY(!g502->skipped());
        QVERIFY(g502->ops[0].expect.kind == ExpectKind::Ok);
        QCOMPARE(g502->ops[0].request.head.number, 8000u);
        QVERIFY(g503->skipped());
        QVERIFY2(g503->skipReason.contains(QStringLiteral("D7999-D8000 spans the general and the "
                                                          "special range")),
                 qPrintable(g503->skipReason));
        // G5-04 (M7680, between deviceEnd M7679 and the special M8000) still expects plcError.
        const ResolvedStep* g504 = findStep(r, QStringLiteral("G5-04"));
        QVERIFY(g504 != nullptr && !g504->skipped());
        QVERIFY(g504->ops[0].expect.kind == ExpectKind::PlcError);

        // The dry run shows both, and the safety gate passes.
        Options options;
        options.profilePath = exampleProfilePath(profile);
        options.planPath =
            testsDir() + QStringLiteral("/hil/plans/") + plan + QStringLiteral(".json");
        options.dryRun = true;
        const ToolRun run = runToolWith(options);
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.err + run.out.left(2000)));
        QVERIFY(run.out.contains(QStringLiteral("safety gate OK")));
        QVERIFY2(run.out.contains(QStringLiteral("STEP G5-03 [read] skipped: D7999-D8000 spans")),
                 qPrintable(run.out));
        QVERIFY(run.out.contains(QStringLiteral("    expect ok, not plcError: D8000 is in the "
                                                "special range from D8000 (specialFrom)\n")));
    }

    // Without specialFrom (Q, FX5U, and the FX3 examples with the key removed) every plan resolves
    // as it did before the key existed: each read or write keeps the plan's expectation, nothing
    // is skipped for a special range, and G5-02 / G5-03 expect plcError.
    void HIL_01_aProfileWithoutSpecialFromResolvesAsBefore_data() {
        QTest::addColumn<QString>("plan");
        QTest::addColumn<QString>("profile");
        QTest::newRow("3E binary") << "qna_ethernet" << "q03ude-eth-3e-bin";
        QTest::newRow("3E ascii") << "qna_ethernet" << "fx5u-eth-3e-ascii";
        QTest::newRow("3C format 4") << "qna_serial" << "q03ude-c24-3c-f4";
        QTest::newRow("1E binary") << "a1e" << "fx3-eth-1e-bin";
        QTest::newRow("1C format 1") << "a1c" << "fx3-serial-1c-f1";
    }
    void HIL_01_aProfileWithoutSpecialFromResolvesAsBefore() {
        QFETCH(QString, plan);
        QFETCH(QString, profile);
        const PlanLoad p = loadPlanNamed(qPrintable(plan));
        QVERIFY(p.ok());
        QJsonObject root = readJsonFile(exampleProfilePath(profile));
        QJsonObject profileObj = root.value(QStringLiteral("profile")).toObject();
        profileObj.remove(QStringLiteral("specialFrom"));
        root.insert(QStringLiteral("profile"), profileObj);
        const ProfileLoad load = loadProfile(root);
        QVERIFY2(load.ok(), qPrintable(load.error.text()));
        const Profile& prof = *load.profile;
        for (size_t i = 0; i < static_cast<size_t>(DeviceType::Count); ++i) {
            QVERIFY(!prof.specialFrom[i]);
        }
        const ResolveResult r = resolvePlan(*p.plan, prof);
        QVERIFY2(r.ok(), qPrintable(r.errors.isEmpty() ? QString() : r.errors[0].text()));
        int compared = 0;
        for (const ResolvedStep& rs : r.steps) {
            QVERIFY2(!rs.skipReason.contains(QStringLiteral("special")), qPrintable(rs.id));
            for (const ResolvedOp& op : rs.ops) {
                QVERIFY2(op.expectNote.isEmpty(), qPrintable(op.recordId));
            }
            if (rs.skipped() || (rs.kind != StepKind::Read && rs.kind != StepKind::Write)) {
                continue;
            }
            const Step* ps = nullptr;
            for (const Step& s : p.plan->steps) {
                if (s.id == rs.id ||
                    (!s.expandOver.isEmpty() && rs.id.startsWith(s.id + QLatin1Char('-')))) {
                    ps = &s;
                }
            }
            QVERIFY2(ps != nullptr, qPrintable(rs.id));
            QVERIFY2(rs.ops[0].expect.kind == ps->op.expect.kind, qPrintable(rs.id));
            ++compared;
        }
        QVERIFY2(compared > 30, qPrintable(QString::number(compared)));
        for (const char* id : {"G5-02", "G5-03"}) {
            const ResolvedStep* s = findStep(r, QLatin1String(id));
            QVERIFY2(s != nullptr && !s->skipped(), id);
            QVERIFY2(s->ops[0].expect.kind == ExpectKind::PlcError, id);
        }
        QVERIFY(!dryRunText(r, prof).contains(QStringLiteral("specialFrom")));
    }

    void HIL_01_theG9ProbesAreLiteralAndReadOnly() {
        const PlanLoad e = loadPlanNamed("qna_ethernet");
        const PlanLoad s = loadPlanNamed("qna_serial");
        QVERIFY(e.ok() && s.ok());
        int raws = 0;
        for (const PlanLoad* p : {&e, &s}) {
            for (const Step& st : p->plan->steps) {
                if (st.id.startsWith(QStringLiteral("G9-01")) || st.id == QLatin1String("G9-07")) {
                    ++raws;
                    QVERIFY2(st.kind == StepKind::Raw && st.readOnly && st.rawRequests.isEmpty(),
                             qPrintable(st.id));
                }
            }
        }
        QCOMPARE(raws, 7); // 3E: G9-01-B, G9-01-A, G9-07; 3C: G9-01-F1..F4
    }

    void HIL_01_theSafetyRelevantStepsAreChecked() {
        // A plan with a write outside scratch is refused whatever else is in it; the plans
        // themselves write only through scratch-relative references or the GV literals D100 / M100.
        for (const char* file : kPlanFiles) {
            const PlanLoad p = loadPlanNamed(file);
            QVERIFY(p.ok());
            for (const Step& st : p.plan->steps) {
                if (st.kind == StepKind::Write && st.op.device.base == DeviceRef::Base::Literal) {
                    QVERIFY2(st.id.startsWith(QStringLiteral("GV-")), qPrintable(st.id));
                }
            }
        }
    }
};

} // namespace

QObject* makePlansSuite() { return new HilPlansTests; }

} // namespace mc::hil::test

#include "tst_hil_plans.moc"
