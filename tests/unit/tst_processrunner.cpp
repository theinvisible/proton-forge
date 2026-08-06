// ProcessRunner exists because every hand-rolled QProcess call site in this
// codebase made the same mistake: it dropped the return value of
// waitForFinished(), so a timeout came back as empty-but-successful output. That
// is what made "System Information" disappear from the Help menu and what let a
// slow kscreen-doctor report "HDR disabled" on a machine with HDR on
// (TESTS.md §7).
//
// The whole contract is therefore "failure is null, success is non-null", and
// these cases pin exactly that. The subprocesses used are /bin/sh, true, false
// and sleep — no network, no display, nothing to install.

#include <QTest>
#include <QElapsedTimer>
#include <QProcessEnvironment>

#include "utils/ProcessRunner.h"

class TstProcessRunner : public QObject
{
    Q_OBJECT

private slots:
    void successReturnsOutput();
    void successWithNoOutputIsEmptyButNotNull();
    void missingProgramIsNull();
    void nonZeroExitIsNull();
    void timeoutIsNullAndDoesNotBlockForLong();
    void environmentIsPassedThrough();
};

void TstProcessRunner::successReturnsOutput()
{
    const QString out = ProcessRunner::run("/bin/sh", {"-c", "printf hello"});

    QVERIFY(!out.isNull());
    QCOMPARE(out, QString("hello"));
}

void TstProcessRunner::successWithNoOutputIsEmptyButNotNull()
{
    // The distinction the whole class is for: "ran fine, said nothing" must not
    // look like "could not run". A caller that only checked isEmpty() here is
    // exactly how a timed-out probe got read as a negative answer.
    const QString out = ProcessRunner::run("/bin/sh", {"-c", "true"});

    QVERIFY(!out.isNull());
    QVERIFY(out.isEmpty());
}

void TstProcessRunner::missingProgramIsNull()
{
    const QString out = ProcessRunner::run("protonforge-no-such-program-exists", {});

    QVERIFY(out.isNull());
}

void TstProcessRunner::nonZeroExitIsNull()
{
    // Output on a failed run is not trustworthy — a partially written answer is
    // worse than no answer, because it parses.
    const QString out = ProcessRunner::run("/bin/sh", {"-c", "printf partial; exit 3"});

    QVERIFY(out.isNull());
}

void TstProcessRunner::timeoutIsNullAndDoesNotBlockForLong()
{
    QElapsedTimer timer;
    timer.start();
    const QString out = ProcessRunner::run("/bin/sh", {"-c", "printf early; sleep 30"}, 200);
    const qint64 elapsed = timer.elapsed();

    // Null even though the child had already printed something before hanging.
    QVERIFY(out.isNull());

    // The child is killed rather than left to ~QProcess, which would block on it.
    // Allow generous slack for a loaded CI machine, but nowhere near the 30 s
    // sleep — that would mean the kill did not happen.
    QVERIFY2(elapsed < 5000, qPrintable(QString("took %1 ms").arg(elapsed)));
}

void TstProcessRunner::environmentIsPassedThrough()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PROTONFORGE_TEST_VALUE", "42");

    const QString out = ProcessRunner::run("/bin/sh", {"-c", "printf %s \"$PROTONFORGE_TEST_VALUE\""},
                                           3000, &env);

    QCOMPARE(out, QString("42"));
}

QTEST_MAIN(TstProcessRunner)
#include "tst_processrunner.moc"
