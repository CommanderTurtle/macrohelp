#include "mainwindow.h"
#include <QCoreApplication>
#include <QDebug>

std::shared_ptr<MainWindow> MainWindow::s_singleton = nullptr;

std::shared_ptr<MainWindow> MainWindow::getInstance(QWidget *parent)
{
    if(s_singleton == nullptr)
    {
        s_singleton = std::shared_ptr<MainWindow>(new MainWindow(parent));
    }
    return s_singleton;
}

MainWindow::MainWindow(QWidget *parent)
    : QObject(parent)
{
}

void MainWindow::autoRun(const QString &filename, int delay, int loopTimes)
{
    // Forward to any connected listener (e.g., the HTTP daemon)
    emit taskRequested(filename, delay, loopTimes);
}

void MainWindow::forceQuit()
{
    // In the sidecar context, quitting the daemon on a "quit self" macro
    // would be surprising and dangerous. Instead, we log and ignore.
    // If desired, connect this signal to QCoreApplication::quit externally.
    qDebug() << "[MainWindow stub] forceQuit() called — ignored in sidecar mode";
}
