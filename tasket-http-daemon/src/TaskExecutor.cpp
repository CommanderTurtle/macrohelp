#include "TaskExecutor.h"

TaskExecutor::TaskExecutor(QObject *parent)
    : QThread(parent)
{
}

TaskExecutor::~TaskExecutor()
{
    stopGracefully();
    m_actionsList.clear();
    quit();
    requestInterruption();
    wait(3000);
}

void TaskExecutor::loadFromTask(const std::shared_ptr<Task> &task)
{
    m_actionsList.clear();
    if(!task)
        return;

    // Use public deepCopy() — this is the key difference from TaskThread,
    // which uses the private copyActionsList() method.
    for(const auto &act : task->m_actionsOrderedList)
    {
        if(act)
            m_actionsList.append(act->deepCopy());
    }
}

void TaskExecutor::setLoop(bool loop, unsigned int timesToRun)
{
    m_loop = loop;
    m_timesToRun = timesToRun;
}

void TaskExecutor::stopGracefully()
{
    m_haveToStop = true;
    terminate();
}

void TaskExecutor::run()
{
begin:
    for(const auto &act : m_actionsList)
    {
        if(m_haveToStop)
            return;
        if(!act)
            continue;

        emit sendRunningStateAct(act->getRefID());
        act->runAction();

        if(m_haveToStop)
            return;
        emit sendDoneStateAct(act->getRefID());
    }

    --m_timesToRun;
    if(m_loop || m_timesToRun > 0)
    {
        emit sendFinishedOneLoop();
        goto begin;
    }

    emit sendFinishedOneLoop();
    emit sendFinishedAllLoops();
}
