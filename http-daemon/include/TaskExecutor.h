#ifndef TASKEXECUTOR_H
#define TASKEXECUTOR_H

#include "Task.h"
#include <QThread>
#include <QList>
#include <memory>

/**
 * @brief Independent task executor that does NOT depend on TaskThread.
 *
 * TaskThread::copyActionsList() is private and only accessible to TaskTab
 * (friend). Rather than patching Tasket++ source, this class reimplements
 * the execution logic using only public AbstractAction APIs (deepCopy,
 * runAction). It provides the same behaviour: loop control and finished signals.
 */
class TaskExecutor : public QThread
{
    Q_OBJECT

public:
    explicit TaskExecutor(QObject *parent = nullptr);
    ~TaskExecutor();

    /**
     * @brief Load actions from a Task via public deepCopy().
     */
    void loadFromTask(const std::shared_ptr<Task> &task);

    /**
     * @brief Set loop mode. loop=true means infinite; otherwise runs
     *        timesToRun iterations.
     */
    void setLoop(bool loop, unsigned int timesToRun = 1);

    void stopGracefully();

    bool isStopping() const { return m_haveToStop; }

signals:
    void sendRunningStateAct(unsigned int actId);
    void sendDoneStateAct(unsigned int actId);
    void sendFinishedOneLoop();
    void sendFinishedAllLoops();

protected:
    void run() override;

private:
    QList<std::shared_ptr<AbstractAction>> m_actionsList;
    bool m_loop = false;
    unsigned int m_timesToRun = 1;
    bool m_haveToStop = false;
};

#endif // TASKEXECUTOR_H
