#include "utils/ProcessRunner.h"

#include <QProcess>
#include <QProcessEnvironment>

namespace ProcessRunner {

QString run(const QString& program, const QStringList& args, int timeoutMs,
            const QProcessEnvironment* env)
{
    QProcess process;
    if (env)
        process.setProcessEnvironment(*env);

    process.start(program, args);

    // A missing binary never starts; without this check the timeout below would
    // be spent waiting for a process that does not exist.
    if (!process.waitForStarted(timeoutMs))
        return QString();

    if (!process.waitForFinished(timeoutMs)) {
        // Kill explicitly. Letting ~QProcess deal with it makes the destructor
        // block on the still-running child.
        process.kill();
        process.waitForFinished(1000);
        return QString();
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return QString();

    // A process that printed nothing hands back a null QByteArray, and
    // QString::fromUtf8 would turn that into a null QString — indistinguishable
    // from failure. Force it to empty-but-non-null so the contract holds.
    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    return output.isNull() ? QStringLiteral("") : output;
}

} // namespace ProcessRunner
