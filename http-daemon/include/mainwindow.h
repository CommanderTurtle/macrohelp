#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QObject>
#include <QString>
#include <QWidget>
#include <memory>

/**
 * @brief Minimal stub of MainWindow for the HTTP trigger sidecar.
 *
 * Tasket++ actions depend on MainWindow::getInstance():
 *   - SystemCommandAction connects forceQuitProgramRequest -> forceQuit()
 *   - RunningOtherTaskAction connects runAnotherTaskRequest -> autoRun()
 *
 * This stub satisfies those dependencies without the Qt GUI. It is a
 * singleton QObject so signals can connect to its slots.
 */
class MainWindow : public QObject
{
    Q_OBJECT

public:
    static std::shared_ptr<MainWindow> getInstance(QWidget *parent = nullptr);
    MainWindow(MainWindow &other) = delete;
    void operator=(const MainWindow &) = delete;
    ~MainWindow() = default;

public slots:
    /**
     * @brief Called by RunningOtherTaskAction to queue another task.
     */
    void autoRun(const QString &filename, int delay, int loopTimes = 1);

    /**
     * @brief Called by SystemCommandAction (QuitSelfProgram).
     *        In the sidecar, this is a no-op — we don't quit the daemon.
     */
    void forceQuit();

signals:
    /**
     * @brief Emitted when autoRun is called — the HTTP daemon can connect
     *        this to schedule the referenced task.
     */
    void taskRequested(const QString &filename, int delay, int loopTimes);

private:
    explicit MainWindow(QWidget *parent = nullptr);
    static std::shared_ptr<MainWindow> s_singleton;
};

#endif // MAINWINDOW_H
