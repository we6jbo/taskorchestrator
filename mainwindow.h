#pragma once

#include <QMainWindow>
#include "orchestrator.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void refreshTasks();
    void runSelected();

private:
    Ui::MainWindow *ui;
    Orchestrator orchestrator;
};
