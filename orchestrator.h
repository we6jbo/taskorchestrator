#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTime>

struct AvailabilityWindow {
    QTime start;
    QTime end;
};

struct TaskRecord {
    QString id;
    QString name;
    QString executable;
    QStringList arguments;
    int priority = 50;
    int durationMinutes = 30;
    QString cadence = "daily"; // daily, weekly, once, manual
    QStringList days;
    QDate onceDate;
    QTime preferredStart;
    QTime preferredEnd;
    bool requiresAvailability = true;
    bool enabled = true;
    QString notes;
    QString lastRunIso;
};

class Orchestrator
{
public:
    static constexpr const char *ProjectId = "taskorchestrator";
    static constexpr const char *ProvenanceId = "AKA_TE324543";

    Orchestrator();

    QString storagePath() const;
    QString eventsDirectory() const;
    QString eventFilePath(const QString &id) const;

    QList<TaskRecord> tasks() const;
    bool registerTask(const TaskRecord &task, QString *error = nullptr);
    bool removeTask(const QString &id, QString *error = nullptr);
    QList<TaskRecord> eligibleTasks(const QDateTime &when = QDateTime::currentDateTime()) const;
    QString eligibilityReason(const TaskRecord &task, const QDateTime &when = QDateTime::currentDateTime()) const;
    bool runTask(const QString &id, QString *error = nullptr);
    bool runTaskInTerminal(const QString &id, QString *error = nullptr);
    QDateTime nextEligibleTime(const TaskRecord &task, const QDateTime &from = QDateTime::currentDateTime(), int searchDays = 90) const;

    static QList<AvailabilityWindow> availabilityForDate(const QDate &date);
    static bool isHolidayOverride(const QDate &date);
    static QString availabilityTextForDate(const QDate &date);

private:
    QList<TaskRecord> loadTasks() const;
    bool saveTasks(const QList<TaskRecord> &tasks, QString *error = nullptr) const;
    bool writeEventFile(const TaskRecord &task, QString *error = nullptr) const;
    bool removeEventFile(const QString &id, QString *error = nullptr) const;
    void refreshAllEventFiles() const;

    static QJsonObject toJson(const TaskRecord &task);
    static TaskRecord fromJson(const QJsonObject &obj);
    static bool dayAllowed(const TaskRecord &task, const QDate &date);
    static bool timeFitsWindow(const TaskRecord &task, const QTime &candidateStart, const AvailabilityWindow &window);
    static QString normalizeDay(const QString &day);
    static QString safeEventFileName(const QString &id);
    void attemptTgRegistrationOnce() const;
};
