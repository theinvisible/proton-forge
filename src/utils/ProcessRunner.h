#ifndef PROCESSRUNNER_H
#define PROCESSRUNNER_H

#include <QString>
#include <QStringList>

class QProcessEnvironment;

// One correct way to run a short-lived helper program and read its output.
//
// Every hand-rolled QProcess call site in this codebase got the same detail
// wrong: the return value of waitForFinished() was dropped, so a timeout read
// back as an empty-but-successful result. That is how "System Information"
// disappeared from the Help menu (see TESTS.md §7) and how a slow
// kscreen-doctor reported "HDR disabled" on a machine with HDR on.
//
// Failure is therefore expressed as a *null* QString, which an empty successful
// output can never be mistaken for.
namespace ProcessRunner {

// Runs `program args` and returns its standard output.
//
// Returns a null QString when the program is missing, fails to start, exceeds
// `timeoutMs` (the child is killed rather than left to the destructor), or exits
// non-zero. A successful run that printed nothing returns an empty — but
// non-null — string, so callers can tell the two apart:
//
//     const QString out = ProcessRunner::run("kscreen-doctor", {"-o"});
//     if (out.isNull())
//         return;            // could not ask; say nothing rather than guess
//
// `env` is passed to the child when non-null; otherwise it inherits ours.
QString run(const QString& program, const QStringList& args, int timeoutMs = 3000,
            const QProcessEnvironment* env = nullptr);

} // namespace ProcessRunner

#endif // PROCESSRUNNER_H
