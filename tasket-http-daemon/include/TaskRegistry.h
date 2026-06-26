#ifndef TASKREGISTRY_H
#define TASKREGISTRY_H

#include "Types.h"
#include "Task.h"
#include "TaskThread.h"

#include <QObject>
#include <QMap>
#include <QTimer>
#include <QMutex>
#include <memory>

/**
 * @brief Thread-safe registry of all task instances.
 *
 * Each time /run is called, a TaskInstance is created with a unique
 * monotonically-increasing task number. The registry tracks lifecycle
 * transitions (Scheduled -> Running -> Finished/Stopped/Failed) and
 * answers /check queries with accurate remaining-time estimates.
 */
class TaskRegistry : public QObject
{
    Q_OBJECT

public:
    explicit TaskRegistry(QObject *parent = nullptr);
    ~TaskRegistry();

    /**
     * @brief Schedule a new task instance.
     * @param name       Task name (base name of .scht file).
     * @param path       Absolute path to the .scht file.
     * @param delaySec   Seconds to wait before executing.
     * @param loops      Number of executions (-1 = infinite).
     * @param description Human-readable description from .scht.
     * @return The newly created TaskInstance (already inserted into registry).
     */
    TaskInstance scheduleTask(const QString &name, const QString &path,
                              int delaySec, int loops,
                              const QString &description = QString());

    /**
     * @brief Look up a task by its assigned number.
     */
    std::optional<TaskInstance> getTask(int taskNumber) const;

    /**
     * @brief Update the state of an existing task.
     */
    void updateState(int taskNumber, TaskState newState,
                     const QString &errorMsg = QString());

    /**
     * @brief Mark a task as having started execution.
     */
    void markStarted(int taskNumber);

    /**
     * @brief Mark a task as finished.
     */
    void markFinished(int taskNumber);

    /**
     * @brief Mark a task as failed.
     */
    void markFailed(int taskNumber, const QString &error);

    /**
     * @brief Mark a task as stopped.
     */
    void markStopped(int taskNumber);

    /**
     * @brief Get a human-readable status summary of a task.
     */
    QString taskStatusMessage(int taskNumber) const;

    /**
     * @brief List all tasks matching a given state filter.
     * @param filter If not Idle, only return tasks in that state.
     */
    QList<TaskInstance> listTasks(TaskState filter = TaskState::Idle) const;

    /**
     * @brief Get all tasks in verbose mode.
     */
    QList<TaskInstance> allTasks() const;

    /**
     * @brief Count of currently running tasks.
     */
    int runningCount() const;

    /**
     * @brief Stop all active tasks.
     */
    void stopAll();

    /**
     * @brief Stop a single task by number.
     */
    void stopTask(int taskNumber);

signals:
    /**
     * @brief Emitted when the delay timer expires; the runner should
     *        actually start the TaskThread for this instance.
     */
    void executeRequested(int taskNumber, QString taskPath, int loopTimes);

    /**
     * @brief Emitted when a task transitions to Finished.
     */
    void taskFinished(int taskNumber, QString taskName);

    /**
     * @brief Emitted when a task transitions to Failed.
     */
    void taskFailed(int taskNumber, QString taskName, QString error);

private slots:
    void onDelayTimerExpired();

private:
    mutable QMutex              m_mutex;
    QMap<int, TaskInstance>     m_tasks;
    QMap<int, QTimer*>          m_delayTimers;
    int                         m_nextTaskNumber = 1;

    TaskInstance *mutableTask(int taskNumber);
};

#endif // TASKREGISTRY_H
