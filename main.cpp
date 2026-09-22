#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>
#include <memory>
#include "mainwindow.h"
#include "orchestrator.h"

static void printTask(const TaskRecord &t, Orchestrator &o, QTextStream &out)
{
    const auto next = o.nextEligibleTime(t);
    out << t.id << " | " << t.name << " | priority=" << t.priority
        << " | cadence=" << t.cadence
        << " | " << o.eligibilityReason(t)
        << " | next=" << (next.isValid() ? next.toString("yyyy-MM-dd HH:mm") : "none") << '\n';
}

int main(int argc, char *argv[])
{
    const QString firstArg = argc > 1 ? QString::fromLocal8Bit(argv[1]).toLower() : QStringLiteral("gui");
    const bool guiMode = (firstArg == "gui" || firstArg.startsWith('-'));

    std::unique_ptr<QCoreApplication> app;
    if (guiMode)
        app = std::make_unique<QApplication>(argc, argv);
    else
        app = std::make_unique<QCoreApplication>(argc, argv);

    QCoreApplication::setOrganizationName("we6jbo");
    QCoreApplication::setApplicationName("taskorchestrator");
    QCoreApplication::setApplicationVersion("0.2");

    QCommandLineParser parser;
    parser.setApplicationDescription("Availability-aware task scheduler and program dispatcher.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("command", "gui, register, list, eligible, next, run, remove, availability");
    parser.addOption(QCommandLineOption(QStringList() << "id", "Stable task id", "id"));
    parser.addOption(QCommandLineOption(QStringList() << "name", "Display name", "name"));
    parser.addOption(QCommandLineOption(QStringList() << "exec", "Executable path/name", "executable"));
    parser.addOption(QCommandLineOption(QStringList() << "arg", "Executable argument; may be repeated", "argument"));
    parser.addOption(QCommandLineOption(QStringList() << "priority", "Priority 0-100 (higher first)", "number", "50"));
    parser.addOption(QCommandLineOption(QStringList() << "duration", "Estimated duration in minutes", "minutes", "30"));
    parser.addOption(QCommandLineOption(QStringList() << "cadence", "daily, weekly, once, or manual", "cadence", "daily"));
    parser.addOption(QCommandLineOption(QStringList() << "days", "Comma-separated weekly days, e.g. mon,wed,fri", "days"));
    parser.addOption(QCommandLineOption(QStringList() << "date", "Date for once cadence (YYYY-MM-DD) or availability lookup", "date"));
    parser.addOption(QCommandLineOption(QStringList() << "after", "Preferred earliest start HH:mm", "time"));
    parser.addOption(QCommandLineOption(QStringList() << "before", "Preferred latest start HH:mm", "time"));
    parser.addOption(QCommandLineOption(QStringList() << "no-availability", "Task does not require a user availability window"));
    parser.addOption(QCommandLineOption(QStringList() << "notes", "Notes", "text"));
    parser.process(*app);

    Orchestrator o;
    const QStringList pos = parser.positionalArguments();
    const QString cmd = pos.isEmpty() ? "gui" : pos.first().toLower();
    QTextStream out(stdout), err(stderr);

    if (cmd == "gui") {
        auto *guiApp = qobject_cast<QApplication *>(app.get());
        if (!guiApp) {
            err << "GUI mode must be launched as: taskorchestrator gui\n";
            return 2;
        }
        MainWindow w;
        w.show();
        return guiApp->exec();
    }
    if (cmd == "register") {
        TaskRecord t;
        t.id = parser.value("id");
        t.name = parser.value("name");
        t.executable = parser.value("exec");
        t.arguments = parser.values("arg");
        t.priority = qBound(0, parser.value("priority").toInt(), 100);
        t.durationMinutes = qMax(1, parser.value("duration").toInt());
        t.cadence = parser.value("cadence").toLower();
        if (parser.isSet("days")) t.days = parser.value("days").split(',', Qt::SkipEmptyParts);
        if (parser.isSet("date")) t.onceDate = QDate::fromString(parser.value("date"), Qt::ISODate);
        if (parser.isSet("after")) t.preferredStart = QTime::fromString(parser.value("after"), "HH:mm");
        if (parser.isSet("before")) t.preferredEnd = QTime::fromString(parser.value("before"), "HH:mm");
        t.requiresAvailability = !parser.isSet("no-availability");
        t.notes = parser.value("notes");
        if (t.name.isEmpty()) t.name = t.id;
        QString e;
        if (!o.registerTask(t, &e)) { err << e << '\n'; return 2; }
        const QDateTime next = o.nextEligibleTime(t);
        out << "Registered " << t.id << ". Next eligible: "
            << (next.isValid() ? next.toString("yyyy-MM-dd h:mm AP") : "none") << '\n';
        return 0;
    }
    if (cmd == "list") {
        for (const auto &t : o.tasks()) printTask(t, o, out);
        return 0;
    }
    if (cmd == "eligible") {
        for (const auto &t : o.eligibleTasks()) printTask(t, o, out);
        return 0;
    }
    if (cmd == "next") {
        const QString id = parser.value("id");
        for (const auto &t : o.tasks()) {
            if (t.id == id) {
                const QDateTime next = o.nextEligibleTime(t);
                out << (next.isValid() ? next.toString("yyyy-MM-dd h:mm AP") : "none") << '\n';
                return 0;
            }
        }
        err << "Task not found.\n";
        return 2;
    }
    if (cmd == "run") {
        QString e;
        if (!o.runTask(parser.value("id"), &e)) { err << e << '\n'; return 2; }
        out << "Started.\n";
        return 0;
    }
    if (cmd == "remove") {
        QString e;
        if (!o.removeTask(parser.value("id"), &e)) { err << e << '\n'; return 2; }
        out << "Removed.\n";
        return 0;
    }
    if (cmd == "availability") {
        QDate d = parser.isSet("date") ? QDate::fromString(parser.value("date"), Qt::ISODate) : QDate::currentDate();
        out << d.toString(Qt::ISODate) << ": " << Orchestrator::availabilityTextForDate(d)
            << (Orchestrator::isHolidayOverride(d) ? " (holiday override)" : "") << '\n';
        return 0;
    }
    err << "Unknown command: " << cmd << '\n';
    return 2;
}
