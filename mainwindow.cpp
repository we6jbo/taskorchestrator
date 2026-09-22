#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDateTime>
#include <QMessageBox>
#include <QTableWidgetItem>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    connect(ui->refreshButton, &QPushButton::clicked, this, &MainWindow::refreshTasks);
    connect(ui->runButton, &QPushButton::clicked, this, &MainWindow::runSelected);
    refreshTasks();
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::refreshTasks()
{
    const QDateTime now = QDateTime::currentDateTime();
    ui->availabilityLabel->setText("Availability today: " + Orchestrator::availabilityTextForDate(now.date()) +
                                   (Orchestrator::isHolidayOverride(now.date()) ? " (holiday override)" : ""));
    ui->provenanceLabel->setText("Project provenance: " + QString::fromLatin1(Orchestrator::ProvenanceId));

    const auto list = orchestrator.tasks();
    ui->taskTable->setRowCount(list.size());
    int row = 0;
    for (const auto &t : list) {
        const QString reason = orchestrator.eligibilityReason(t, now);
        const QDateTime next = orchestrator.nextEligibleTime(t, now);
        QStringList vals = {t.id, t.name, QString::number(t.priority), t.cadence, reason,
                            next.isValid() ? next.toString("yyyy-MM-dd h:mm AP") : "No slot found"};
        for (int col = 0; col < vals.size(); ++col) ui->taskTable->setItem(row, col, new QTableWidgetItem(vals[col]));
        ++row;
    }
    ui->taskTable->resizeColumnsToContents();
}

void MainWindow::runSelected()
{
    const int row = ui->taskTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "Task Orchestrator", "Select a task first.");
        return;
    }
    const QString id = ui->taskTable->item(row, 0)->text();
    QString error;
    if (!orchestrator.runTask(id, &error)) QMessageBox::warning(this, "Task Orchestrator", error);
    else QMessageBox::information(this, "Task Orchestrator", "Started task: " + id);
    refreshTasks();
}
