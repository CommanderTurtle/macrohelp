#include "TaskRegistry.h"
#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <QThread>

TaskRegistry::TaskRegistry(QObject *parent)
    : QObject(parent)
{
}

TaskRegistry::~TaskRegistry()
{
    QMutexLocker lock(&m_mutex);
    for(auto *timer : m_delayTimers)
    {
        if(timer)
        {
            timer->stop();
            delete timer;
        }
    }
    m_delayTimers.clear();
}

TaskInstance TaskRegistry::scheduleTask(const QString &name, const QString &path,
                                          int delaySec, int loops,
                                          const QString &description)
{
    if(QThread::currentThread() != thread())
    {
        TaskInstance result;
        QMetaObject::invokeMethod(this, [&]() {
            result = scheduleTask(name, path, delaySec, loops, description);
        }, Qt::BlockingQueuedConnection);
        return result;
    }

    QMutexLocker lock(&m_mutex);

    TaskInstance ti;
    ti.taskNumber       = m_nextTaskNumber++;
    ti.taskName         = name;
    ti.taskPath         = path;
    ti.state            = TaskState::Scheduled;
    ti.delaySeconds     = delaySec;
    ti.loopTimes        = loops;
    ti.createdAt        = QDateTime::currentDateTime();
    ti.scheduledToRunAt = ti.createdAt.addSecs(delaySec);
    ti.description      = description;

    m_tasks.insert(ti.taskNumber, ti);

    // Create a single-shot timer for the delay
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(delaySec * 1000);

    // Store task number in timer property so we know which one fired
    timer->setProperty("taskNumber", ti.taskNumber);
    connect(timer, &QTimer::timeout, this, &TaskRegistry::onDelayTimerExpired);

    m_delayTimers.insert(ti.taskNumber, timer);
    timer->start();

    return ti;
}

std::optional<TaskInstance> TaskRegistry::getTask(int taskNumber) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_tasks.find(taskNumber);
    if(it == m_tasks.end())
        return std::nullopt;
    return *it;
}

void TaskRegistry::updateState(int taskNumber, TaskState newState,
                                const QString &errorMsg)
{
    QString taskName;
    QString err;
    bool shouldEmitFinished = false;
    bool shouldEmitFailed = false;

    {
        QMutexLocker lock(&m_mutex);
        TaskInstance *ti = mutableTask(taskNumber);
        if(!ti)
            return;

        ti->state = newState;
        if(!errorMsg.isEmpty())
            ti->errorMessage = errorMsg;

        if(newState == TaskState::Running && !ti->startedAt.isValid())
            ti->startedAt = QDateTime::currentDateTime();

        if(newState == TaskState::Finished || newState == TaskState::Stopped
            || newState == TaskState::Failed)
        {
            ti->finishedAt = QDateTime::currentDateTime();

            // Clean up delay timer if still present
            auto timerIt = m_delayTimers.find(taskNumber);
            if(timerIt != m_delayTimers.end())
            {
                (*timerIt)->stop();
                (*timerIt)->deleteLater();
                m_delayTimers.erase(timerIt);
            }
        }

        // Collect signal data while holding the lock, but emit outside
        taskName = ti->taskName;
        err = ti->errorMessage;
        shouldEmitFinished = (newState == TaskState::Finished);
        shouldEmitFailed = (newState == TaskState::Failed);
    }

    // Emit signals outside the lock to prevent deadlocks
    if(shouldEmitFinished)
        emit taskFinished(taskNumber, taskName);
    else if(shouldEmitFailed)
        emit taskFailed(taskNumber, taskName, err);
}

void TaskRegistry::markStarted(int taskNumber)
{
    updateState(taskNumber, TaskState::Running);
}

void TaskRegistry::markFinished(int taskNumber)
{
    updateState(taskNumber, TaskState::Finished);
}

void TaskRegistry::markFailed(int taskNumber, const QString &error)
{
    updateState(taskNumber, TaskState::Failed, error);
}

void TaskRegistry::markStopped(int taskNumber)
{
    updateState(taskNumber, TaskState::Stopped);
}

QString TaskRegistry::taskStatusMessage(int taskNumber) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_tasks.find(taskNumber);
    if(it == m_tasks.end())
        return QString("Task #%1 not found").arg(taskNumber);
    return it->checkMessage();
}

QList<TaskInstance> TaskRegistry::listTasks(TaskState filter) const
{
    QMutexLocker lock(&m_mutex);
    QList<TaskInstance> result;
    for(const auto &ti : m_tasks)
    {
        if(filter == TaskState::Idle || ti.state == filter)
            result.append(ti);
    }
    // Sort by task number ascending
    std::sort(result.begin(), result.end(),
              [](const TaskInstance &a, const TaskInstance &b) {
                  return a.taskNumber < b.taskNumber;
              });
    return result;
}

QList<TaskInstance> TaskRegistry::allTasks() const
{
    return listTasks(TaskState::Idle); // Idle means "no filter"
}

int TaskRegistry::runningCount() const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for(const auto &ti : m_tasks)
    {
        if(ti.state == TaskState::Running)
            ++count;
    }
    return count;
}

void TaskRegistry::stopAll()
{
    QMutexLocker lock(&m_mutex);
    for(auto it = m_tasks.begin(); it != m_tasks.end(); ++it)
    {
        if(it->state == TaskState::Scheduled || it->state == TaskState::Running)
        {
            it->state = TaskState::Stopped;
            it->finishedAt = QDateTime::currentDateTime();

            auto timerIt = m_delayTimers.find(it.key());
            if(timerIt != m_delayTimers.end())
            {
                (*timerIt)->stop();
                (*timerIt)->deleteLater();
                m_delayTimers.erase(timerIt);
            }
        }
    }
}

void TaskRegistry::stopTask(int taskNumber)
{
    QMutexLocker lock(&m_mutex);
    TaskInstance *ti = mutableTask(taskNumber);
    if(!ti)
        return;

    if(ti->state == TaskState::Scheduled || ti->state == TaskState::Running)
    {
        ti->state = TaskState::Stopped;
        ti->finishedAt = QDateTime::currentDateTime();

        auto timerIt = m_delayTimers.find(taskNumber);
        if(timerIt != m_delayTimers.end())
        {
            (*timerIt)->stop();
            (*timerIt)->deleteLater();
            m_delayTimers.erase(timerIt);
        }
    }
}

void TaskRegistry::onDelayTimerExpired()
{
    QTimer *timer = qobject_cast<QTimer*>(sender());
    if(!timer)
        return;

    int taskNumber = timer->property("taskNumber").toInt();

    {
        QMutexLocker lock(&m_mutex);
        TaskInstance *ti = mutableTask(taskNumber);
        if(!ti)
            return;
        if(ti->state != TaskState::Scheduled)
            return; // Was stopped or modified

        ti->state = TaskState::Running;
        ti->startedAt = QDateTime::currentDateTime();

        // Remove timer from map but don't delete yet — we'll deleteLater after emit
        auto timerIt = m_delayTimers.find(taskNumber);
        if(timerIt != m_delayTimers.end())
            m_delayTimers.erase(timerIt);
        timer->deleteLater();
    }

    // Fetch path and loops outside the lock
    auto optTask = getTask(taskNumber);
    if(optTask)
    {
        const TaskInstance &ti = *optTask;
        emit executeRequested(taskNumber, ti.taskPath, ti.loopTimes);
    }
}

TaskInstance *TaskRegistry::mutableTask(int taskNumber)
{
    auto it = m_tasks.find(taskNumber);
    if(it == m_tasks.end())
        return nullptr;
    return &(*it);
}
