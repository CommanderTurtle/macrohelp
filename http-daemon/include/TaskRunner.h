#ifndef TASKRUNNER_H
#define TASKRUNNER_H

#include "Types.h"
#include "TaskRegistry.h"
#include "TaskExecutor.h"

#include <QObject>
#include <QMap>
#include <memory>

/**
 * @brief Bridges the HTTP trigger to the Tasket++ automation engine.
 *
 * TaskRunner receives execute requests from TaskRegistry, loads the .scht
 * file via TaskJsonLoader, deep-copies actions into a TaskExecutor, and
 * starts it. TaskExecutor is our own QThread that does NOT depend on
 * TaskThread's private copyActionsList() method.
 */
class TaskRunner : public QObject
{
    Q_OBJECT

public:
    explicit TaskRunner(TaskRegistry *registry, QObject *parent = nullptr);
    ~TaskRunner();

public slots:
    /**
     * @brief Actually execute a task (called when delay timer expires).
     */
    void executeTask(int taskNumber, QString taskPath, int loopTimes);

    /**
     * @brief Stop all running executors.
     */
    void stopAllTasks();

    /**
     * @brief Stop a specific task executor.
     */
    void stopTaskThread(int taskNumber);

signals:
    void taskExecutionStarted(int taskNumber, QString taskName);
    void taskExecutionFinished(int taskNumber, QString taskName);

private slots:
    void onTaskFinished();

private:
    TaskRegistry *m_registry = nullptr;
    // Maps taskNumber -> TaskExecutor*
    QMap<int, TaskExecutor*> m_executors;
};

#endif // TASKRUNNER_H
