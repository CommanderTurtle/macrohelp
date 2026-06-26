#ifndef TASKJSONLOADER_H
#define TASKJSONLOADER_H

#include "Task.h"
#include <QJsonObject>
#include <QString>
#include <memory>

class AbstractAction;

/**
 * @brief Standalone task loader that parses Tasket++ .scht JSON files
 *        into executable Task objects without any UI dependency.
 */
class TaskJsonLoader
{
public:
    TaskJsonLoader();

    std::shared_ptr<Task> loadTaskFromFile(const QString &path);
    std::shared_ptr<Task> loadTaskFromJson(const QString &jsonContent);

    std::shared_ptr<AbstractAction> jsonToAction(const QJsonObject &jobj);
    QJsonObject actionToJson(const std::shared_ptr<AbstractAction> &act);

    QString lastError() const { return m_lastError; }

private:
    QString m_lastError;
};

#endif // TASKJSONLOADER_H
