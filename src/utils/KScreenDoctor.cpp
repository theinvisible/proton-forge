#include "utils/KScreenDoctor.h"

#include "utils/ProcessRunner.h"

#include <QRegularExpression>
#include <QStandardPaths>

bool KScreenDoctor::available()
{
    return !QStandardPaths::findExecutable("kscreen-doctor").isEmpty();
}

QString KScreenDoctor::stripAnsi(QString text)
{
    // SGR escape sequences: ESC [ <params> m. "\x1b" and "\033" are the same
    // byte — the old code applied both spellings of this regex in a row.
    static const QRegularExpression sgr("\x1b\\[[0-9;]*m");
    return text.remove(sgr);
}

QString KScreenDoctor::run()
{
    if (!available())
        return QString();

    const QString output = ProcessRunner::run("kscreen-doctor", {"-o"});
    if (output.isNull())
        return QString();

    return stripAnsi(output);
}
