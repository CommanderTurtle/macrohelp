#include "TaskRunner.h"
#include "TaskJsonLoader.h"
#include <QFile>

TaskRunner::TaskRunner(TaskRegistry *registry, QObject *parent)
    : QObject(parent), m_registry(registry)
{
}

TaskRunner::~TaskRunner()
{
    stopAllTasks();
}

void TaskRunner::executeTask(int taskNumber, QString taskPath, int loopTimes)
{
    if(!m_registry)
        return;

    if(!QFile::exists(taskPath))
    {
        m_registry->markFailed(taskNumber, QString("Task file not found: %1").arg(taskPath));
        return;
    }

    TaskJsonLoader loader;
    auto task = loader.loadTaskFromFile(taskPath);
    if(!task)
    {
        m_registry->markFailed(taskNumber, loader.lastError());
        return;
    }

    // Use TaskExecutor instead of TaskThread — avoids private copyActionsList()
    TaskExecutor *executor = new TaskExecutor();
    executor->loadFromTask(task);

    if(loopTimes < 0)
        executor->setLoop(true, 1);
    else
        executor->setLoop(false, static_cast<unsigned int>(loopTimes));

    connect(executor, &TaskExecutor::sendFinishedAllLoops, this, &TaskRunner::onTaskFinished);
    connect(executor, &TaskExecutor::finished, executor, &TaskExecutor::deleteLater);

    m_executors.insert(taskNumber, executor);
    m_registry->markStarted(taskNumber);

    executor->start();

    auto optTask = m_registry->getTask(taskNumber);
    if(optTask)
        emit taskExecutionStarted(taskNumber, optTask->taskName);
}

void TaskRunner::stopAllTasks()
{
    for(auto it = m_executors.begin(); it != m_executors.end(); ++it)
    {
        TaskExecutor *exec = it.value();
        if(exec)
            exec->stopGracefully();
        if(m_registry)
            m_registry->markStopped(it.key());
    }
    m_executors.clear();
}

void TaskRunner::stopTaskThread(int taskNumber)
{
    auto it = m_executors.find(taskNumber);
    if(it != m_executors.end())
    {
        TaskExecutor *exec = it.value();
        if(exec)
            exec->stopGracefully();
        m_executors.erase(it);
        if(m_registry)
            m_registry->markStopped(taskNumber);
    }
}

void TaskRunner::onTaskFinished()
{
    TaskExecutor *executor = qobject_cast<TaskExecutor*>(sender());
    if(!executor)
        return;

    // Find which task number owns this executor
    int taskNumber = -1;
    for(auto it = m_executors.begin(); it != m_executors.end(); ++it)
    {
        if(it.value() == executor)
        {
            taskNumber = it.key();
            break;
        }
    }

    if(taskNumber < 0)
        return; // Already removed by stopTaskThread() — don't double-transition

    m_executors.remove(taskNumber);

    // Only mark finished if the task wasn't already stopped externally
    auto optTask = m_registry->getTask(taskNumber);
    if(optTask && optTask->state != TaskState::Stopped)
    {
        m_registry->markFinished(taskNumber);
        emit taskExecutionFinished(taskNumber, optTask->taskName);
    }
}
