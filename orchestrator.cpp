#include "orchestrator.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>
#include <algorithm>

namespace {
QString appDataDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + "/.local/share/taskorchestrator";
    QDir().mkpath(base);
    return base;
}

QSet<QDate> holidayDates()
{
    return {
        QDate(2026, 7, 3), QDate(2026, 9, 7), QDate(2026, 11, 11),
        QDate(2026, 11, 26), QDate(2026, 11, 27), QDate(2026, 12, 24),
        QDate(2026, 12, 25), QDate(2026, 12, 31), QDate(2027, 1, 1),
        QDate(2027, 1, 18), QDate(2027, 2, 12), QDate(2027, 2, 15),
        QDate(2027, 5, 31), QDate(2027, 6, 18)
    };
}

QString dayToken(Qt::DayOfWeek d)
{
    switch (d) {
    case Qt::Monday: return "mon";
    case Qt::Tuesday: return "tue";
    case Qt::Wednesday: return "wed";
    case Qt::Thursday: return "thu";
    case Qt::Friday: return "fri";
    case Qt::Saturday: return "sat";
    case Qt::Sunday: return "sun";
    }
    return {};
}
}

Orchestrator::Orchestrator()
{
    QDir().mkpath(eventsDirectory());
    attemptTgRegistrationOnce();
    refreshAllEventFiles();
}

QString Orchestrator::storagePath() const
{
    return appDataDir() + "/tasks.json";
}

QString Orchestrator::eventsDirectory() const
{
    return QDir::homePath() + "/.taskorchestrator/events";
}

QString Orchestrator::eventFilePath(const QString &id) const
{
    return eventsDirectory() + "/" + safeEventFileName(id) + ".json";
}

QList<TaskRecord> Orchestrator::tasks() const
{
    return loadTasks();
}

bool Orchestrator::registerTask(const TaskRecord &task, QString *error)
{
    if (task.id.trimmed().isEmpty() || task.executable.trimmed().isEmpty()) {
        if (error) *error = "Task id and executable are required.";
        return false;
    }

    QList<TaskRecord> list = loadTasks();
    TaskRecord saved = task;
    bool replaced = false;
    for (TaskRecord &existing : list) {
        if (existing.id == task.id) {
            if (saved.lastRunIso.isEmpty())
                saved.lastRunIso = existing.lastRunIso;
            existing = saved;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        list.append(saved);

    if (!saveTasks(list, error))
        return false;
    return writeEventFile(saved, error);
}

bool Orchestrator::removeTask(const QString &id, QString *error)
{
    QList<TaskRecord> list = loadTasks();
    const int before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const TaskRecord &t){ return t.id == id; }), list.end());
    if (list.size() == before) {
        if (error) *error = "No task with id '" + id + "'.";
        return false;
    }
    if (!saveTasks(list, error))
        return false;
    return removeEventFile(id, error);
}

QList<TaskRecord> Orchestrator::eligibleTasks(const QDateTime &when) const
{
    QList<TaskRecord> out;
    for (const TaskRecord &t : loadTasks()) {
        if (eligibilityReason(t, when).startsWith("ELIGIBLE"))
            out.append(t);
    }
    std::sort(out.begin(), out.end(), [](const TaskRecord &a, const TaskRecord &b){
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.name.toLower() < b.name.toLower();
    });
    return out;
}

QString Orchestrator::eligibilityReason(const TaskRecord &task, const QDateTime &when) const
{
    if (!task.enabled)
        return "NOT ELIGIBLE: disabled";
    if (!dayAllowed(task, when.date()))
        return "NOT ELIGIBLE: cadence/day constraint";
    if (!task.requiresAvailability)
        return "ELIGIBLE: availability not required";

    const auto windows = availabilityForDate(when.date());
    if (windows.isEmpty())
        return "NOT ELIGIBLE: no availability window";

    for (const auto &w : windows) {
        if (timeFitsWindow(task, when.time(), w))
            return "ELIGIBLE: inside availability window";
    }
    return "NOT ELIGIBLE: outside availability window";
}

bool Orchestrator::runTask(const QString &id, QString *error)
{
    QList<TaskRecord> list = loadTasks();
    for (TaskRecord &t : list) {
        if (t.id != id) continue;
        if (!t.enabled) {
            if (error) *error = "Task is disabled.";
            return false;
        }
        const bool ok = QProcess::startDetached(t.executable, t.arguments);
        if (!ok) {
            if (error) *error = "Could not start: " + t.executable;
            return false;
        }
        t.lastRunIso = QDateTime::currentDateTime().toString(Qt::ISODate);
        if (!saveTasks(list, error))
            return false;
        writeEventFile(t, nullptr);
        return true;
    }
    if (error) *error = "No task with id '" + id + "'.";
    return false;
}

bool Orchestrator::runTaskInTerminal(const QString &id, QString *error)
{
    bool found = false;
    for (const TaskRecord &t : loadTasks()) {
        if (t.id == id) {
            found = true;
            if (!t.enabled) {
                if (error) *error = "Task is disabled.";
                return false;
            }
            break;
        }
    }
    if (!found) {
        if (error) *error = "No task with id '" + id + "'.";
        return false;
    }

    const QString exe = QCoreApplication::applicationFilePath();
    struct Choice { QString name; QStringList args; };
    const QList<Choice> choices = {
        {"konsole", {"-e", exe, "run", "--id", id}},
        {"gnome-terminal", {"--", exe, "run", "--id", id}},
        {"kitty", {exe, "run", "--id", id}},
        {"xterm", {"-e", exe, "run", "--id", id}}
    };

    for (const Choice &choice : choices) {
        const QString terminal = QStandardPaths::findExecutable(choice.name);
        if (!terminal.isEmpty() && QProcess::startDetached(terminal, choice.args))
            return true;
    }

    const QString xfce = QStandardPaths::findExecutable("xfce4-terminal");
    if (!xfce.isEmpty()) {
        const QString cmd = QString("%1 run --id %2").arg(exe, id);
        if (QProcess::startDetached(xfce, {"--command", cmd}))
            return true;
    }

    if (error) *error = "No supported terminal emulator was found (konsole, xfce4-terminal, gnome-terminal, kitty, or xterm).";
    return false;
}

QDateTime Orchestrator::nextEligibleTime(const TaskRecord &task, const QDateTime &from, int searchDays) const
{
    QDate date = from.date();
    for (int i = 0; i <= searchDays; ++i, date = date.addDays(1)) {
        if (!dayAllowed(task, date)) continue;
        auto windows = availabilityForDate(date);
        if (!task.requiresAvailability) {
            QTime candidate = (i == 0 ? from.time() : QTime(0,0));
            if (task.preferredStart.isValid() && candidate < task.preferredStart)
                candidate = task.preferredStart;
            if (task.preferredEnd.isValid() && candidate > task.preferredEnd)
                continue;
            return QDateTime(date, candidate);
        }
        for (const auto &w : windows) {
            QTime candidate = w.start;
            if (i == 0 && from.time() > candidate)
                candidate = from.time();
            if (task.preferredStart.isValid() && candidate < task.preferredStart)
                candidate = task.preferredStart;
            if (timeFitsWindow(task, candidate, w))
                return QDateTime(date, candidate);
        }
    }
    return {};
}

QList<AvailabilityWindow> Orchestrator::availabilityForDate(const QDate &date)
{
    if (isHolidayOverride(date))
        return {{QTime(4,0), QTime(23,0)}};

    switch (date.dayOfWeek()) {
    case Qt::Saturday:
    case Qt::Sunday:
        return {{QTime(7,0), QTime(23,0)}};
    case Qt::Monday:
    case Qt::Tuesday:
        return {{QTime(4,0), QTime(6,30)}, {QTime(15,0), QTime(23,0)}};
    case Qt::Wednesday:
    case Qt::Friday:
        return {{QTime(4,0), QTime(6,30)}, {QTime(17,30), QTime(23,0)}};
    case Qt::Thursday:
        return {{QTime(4,0), QTime(6,0)}, {QTime(16,30), QTime(23,0)}};
    }
    return {};
}

bool Orchestrator::isHolidayOverride(const QDate &date)
{
    return holidayDates().contains(date);
}

QString Orchestrator::availabilityTextForDate(const QDate &date)
{
    QStringList parts;
    for (const auto &w : availabilityForDate(date))
        parts << w.start.toString("h:mm AP") + "-" + w.end.toString("h:mm AP");
    return parts.join(", ");
}

QList<TaskRecord> Orchestrator::loadTasks() const
{
    QFile file(storagePath());
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto doc = QJsonDocument::fromJson(file.readAll());
    QList<TaskRecord> out;
    for (const auto &v : doc.array())
        if (v.isObject()) out.append(fromJson(v.toObject()));
    return out;
}

bool Orchestrator::saveTasks(const QList<TaskRecord> &tasks, QString *error) const
{
    QJsonArray arr;
    for (const TaskRecord &t : tasks) arr.append(toJson(t));
    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool Orchestrator::writeEventFile(const TaskRecord &t, QString *error) const
{
    if (!QDir().mkpath(eventsDirectory())) {
        if (error) *error = "Could not create events directory: " + eventsDirectory();
        return false;
    }

    QJsonArray args;
    for (const auto &a : t.arguments) args.append(a);
    QJsonArray days;
    for (const auto &d : t.days) days.append(normalizeDay(d));

    QJsonObject schedule {
        {"cadence", t.cadence},
        {"days", days},
        {"once_date", t.onceDate.isValid() ? t.onceDate.toString(Qt::ISODate) : ""},
        {"after", t.preferredStart.isValid() ? t.preferredStart.toString("HH:mm") : ""},
        {"before", t.preferredEnd.isValid() ? t.preferredEnd.toString("HH:mm") : ""},
        {"requires_availability", t.requiresAvailability}
    };

    const QDateTime next = nextEligibleTime(t);
    QJsonObject state {
        {"enabled", t.enabled},
        {"last_run", t.lastRunIso},
        {"eligibility", eligibilityReason(t)},
        {"next_eligible", next.isValid() ? next.toString(Qt::ISODate) : ""}
    };

    QJsonObject actions {
        {"run", "taskorchestrator run --id " + t.id},
        {"run_in_terminal", "taskorchestrator run-terminal --id " + t.id}
    };

    QJsonObject event {
        {"schema", 1},
        {"kind", "taskorchestrator_event"},
        {"id", t.id},
        {"name", t.name},
        {"exec", t.executable},
        {"arguments", args},
        {"priority", t.priority},
        {"duration_minutes", t.durationMinutes},
        {"schedule", schedule},
        {"state", state},
        {"actions", actions},
        {"notes", t.notes},
        {"provenance_id", ProvenanceId},
        {"updated_at", QDateTime::currentDateTime().toString(Qt::ISODate)}
    };

    QSaveFile file(eventFilePath(t.id));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(event).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool Orchestrator::removeEventFile(const QString &id, QString *error) const
{
    const QString path = eventFilePath(id);
    if (!QFile::exists(path)) return true;
    if (QFile::remove(path)) return true;
    if (error) *error = "Could not remove event file: " + path;
    return false;
}

void Orchestrator::refreshAllEventFiles() const
{
    for (const TaskRecord &task : loadTasks())
        writeEventFile(task, nullptr);
}

QJsonObject Orchestrator::toJson(const TaskRecord &t)
{
    QJsonArray args; for (const auto &a : t.arguments) args.append(a);
    QJsonArray days; for (const auto &d : t.days) days.append(d);
    return {
        {"id", t.id}, {"name", t.name}, {"executable", t.executable}, {"arguments", args},
        {"priority", t.priority}, {"duration_minutes", t.durationMinutes}, {"cadence", t.cadence},
        {"days", days}, {"once_date", t.onceDate.isValid() ? t.onceDate.toString(Qt::ISODate) : ""},
        {"preferred_start", t.preferredStart.isValid() ? t.preferredStart.toString("HH:mm") : ""},
        {"preferred_end", t.preferredEnd.isValid() ? t.preferredEnd.toString("HH:mm") : ""},
        {"requires_availability", t.requiresAvailability}, {"enabled", t.enabled},
        {"notes", t.notes}, {"last_run", t.lastRunIso}, {"provenance_id", ProvenanceId}
    };
}

TaskRecord Orchestrator::fromJson(const QJsonObject &o)
{
    TaskRecord t;
    t.id = o["id"].toString(); t.name = o["name"].toString(); t.executable = o["executable"].toString();
    for (const auto &v : o["arguments"].toArray()) t.arguments << v.toString();
    t.priority = o["priority"].toInt(50); t.durationMinutes = o["duration_minutes"].toInt(30);
    t.cadence = o["cadence"].toString("daily");
    for (const auto &v : o["days"].toArray()) t.days << v.toString();
    t.onceDate = QDate::fromString(o["once_date"].toString(), Qt::ISODate);
    t.preferredStart = QTime::fromString(o["preferred_start"].toString(), "HH:mm");
    t.preferredEnd = QTime::fromString(o["preferred_end"].toString(), "HH:mm");
    t.requiresAvailability = o["requires_availability"].toBool(true); t.enabled = o["enabled"].toBool(true);
    t.notes = o["notes"].toString(); t.lastRunIso = o["last_run"].toString();
    return t;
}

bool Orchestrator::dayAllowed(const TaskRecord &t, const QDate &date)
{
    const QString cadence = t.cadence.trimmed().toLower();
    if (cadence == "manual" || cadence == "daily") return true;
    if (cadence == "once") return t.onceDate.isValid() && t.onceDate == date;
    if (cadence == "weekly") {
        if (t.days.isEmpty()) return true;
        const QString token = dayToken(static_cast<Qt::DayOfWeek>(date.dayOfWeek()));
        for (const auto &d : t.days) if (normalizeDay(d) == token) return true;
        return false;
    }
    return true;
}

bool Orchestrator::timeFitsWindow(const TaskRecord &t, const QTime &candidateStart, const AvailabilityWindow &w)
{
    if (candidateStart < w.start || candidateStart > w.end) return false;
    if (t.preferredStart.isValid() && candidateStart < t.preferredStart) return false;
    if (t.preferredEnd.isValid() && candidateStart > t.preferredEnd) return false;
    const int secondsNeeded = qMax(1, t.durationMinutes) * 60;
    return candidateStart.secsTo(w.end) >= secondsNeeded;
}

QString Orchestrator::normalizeDay(const QString &day)
{
    const QString d = day.trimmed().toLower();
    if (d.startsWith("mon")) return "mon";
    if (d.startsWith("tue")) return "tue";
    if (d.startsWith("wed")) return "wed";
    if (d.startsWith("thu")) return "thu";
    if (d.startsWith("fri")) return "fri";
    if (d.startsWith("sat")) return "sat";
    if (d.startsWith("sun")) return "sun";
    return d;
}

QString Orchestrator::safeEventFileName(const QString &id)
{
    QString safe = id.trimmed();
    for (int i = 0; i < safe.size(); ++i) {
        const QChar c = safe.at(i);
        if (!(c.isLetterOrNumber() || c == '-' || c == '_' || c == '.'))
            safe[i] = '_';
    }
    if (safe.isEmpty()) safe = "task";
    return safe;
}

void Orchestrator::attemptTgRegistrationOnce() const
{
    const QString marker = appDataDir() + "/tg_registration_attempted";
    if (QFile::exists(marker)) return;
    QProcess p;
    p.start("tg-register-project", {"--project-id", ProjectId,
                                     "--project-root", "/home/we6jbo/Projects/taskorchestrator",
                                     "--codes", ProvenanceId,
                                     "--reference", "taskorchestrator project provenance"});
    p.waitForFinished(3000);
    QFile f(marker);
    if (f.open(QIODevice::WriteOnly))
        f.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
}
