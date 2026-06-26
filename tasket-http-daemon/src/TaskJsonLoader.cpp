#include "TaskJsonLoader.h"
#include "globals.h"
#include "actions/PasteAction.h"
#include "actions/WaitAction.h"
#include "actions/KeysSequenceAction.h"
#include "actions/SystemCommandsAction.h"
#include "actions/CursorMovementsAction.h"
#include "actions/RunningOtherTaskAction.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>

TaskJsonLoader::TaskJsonLoader()
{
}

std::shared_ptr<Task> TaskJsonLoader::loadTaskFromFile(const QString &path)
{
    QFile fileToOpen(path);
    if(!fileToOpen.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        m_lastError = QString("Cannot open file: %1").arg(path);
        return nullptr;
    }

    QString fileContent = fileToOpen.readAll();
    fileToOpen.close();

    return loadTaskFromJson(fileContent);
}

std::shared_ptr<Task> TaskJsonLoader::loadTaskFromJson(const QString &jsonContent)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonContent.toUtf8());
    if(jsonDoc.isNull())
    {
        m_lastError = "JSON parse error: document is null";
        return nullptr;
    }

    QJsonObject jsonObj = jsonDoc.object();

    if(jsonObj.value(G_Files::DocumentIdentification_KeyWord).toString()
        != G_Files::DocumentIdentification_Value)
    {
        m_lastError = "File format error: not a valid Tasket++ task file";
        return nullptr;
    }

    std::shared_ptr<Task> task = std::make_shared<Task>();

    QJsonArray actionsArray = jsonObj.value(G_Files::DocumentActionsArray_KeyWord).toArray();
    for(const auto &jvalue : actionsArray)
    {
        QJsonObject jobj = jvalue.toObject();
        auto actionToAdd = jsonToAction(jobj);
        if(actionToAdd != nullptr)
            task->appendAction(actionToAdd);
    }

    m_lastError.clear();
    return task;
}

std::shared_ptr<AbstractAction> TaskJsonLoader::jsonToAction(const QJsonObject &jobj)
{
    std::shared_ptr<AbstractAction> actionToReturn = nullptr;
    ActionParameters params;

    QString type = jobj.value(G_Files::ActionType_KeyWord).toString();
    if(type == G_Files::ActionPasteType_Value)
    {
        actionToReturn = std::make_shared<PasteAction>();
        params.m_pasteContent = jobj.value(G_Files::ActionContent_KeyWord).toString();
        params.m_dataId = jobj.value(G_Files::ActionContentId_KeyWord).toString();
        params.m_timesToRun = jobj.value(G_Files::ActionPasteTextLoop_KeyWord).toInt(1);
    }
    else if(type == G_Files::ActionWaitType_Value)
    {
        actionToReturn = std::make_shared<WaitAction>();
        params.m_waitDuration = jobj.value(G_Files::ActionDuration_KeyWord).toDouble();
    }
    else if(type == G_Files::ActionKeysSequenceType_Value)
    {
        actionToReturn = std::make_shared<KeysSequenceAction>();
        auto readMap = jobj.value(G_Files::ActionKeysSeqMap_KeyWord).toVariant().toMap();
        PressedReleaseDelaysKeysMap actMap;
        for(const auto &[key, val] : readMap.asKeyValueRange())
        {
            auto jarr = val.toJsonArray();
            if(jarr.size() < 2)
                continue;
            ReleaseDelayKeysPair pair;
            pair.first = jarr[0].toInt();
            pair.second = jarr[1].toVariant().toStringList();
            actMap.insert(key.toInt(), pair);
        }
        params.m_keysSeqMap = actMap;
        params.m_dataId = jobj.value(G_Files::ActionKeysSeqId_KeyWord).toString();
        params.m_timesToRun = jobj.value(G_Files::ActionKeysSeqLoop_KeyWord).toInt();
    }
    else if(type == G_Files::ActionSystemCommandeType_Value)
    {
        actionToReturn = std::make_shared<SystemCommandAction>();
        params.m_sysCmdTypeStr = jobj.value(G_Files::ActionSysCommandType_KeyWord).toString();
        params.m_sysCmdParam1 = jobj.value(G_Files::ActionSysCommandParam1_KeyWord).toString();
        params.m_sysCmdParam2 = jobj.value(G_Files::ActionSysCommandParam2_KeyWord).toString();
    }
    else if(type == G_Files::ActionCursorMovementsType_Value)
    {
        actionToReturn = std::make_shared<CursorMovementsAction>();
        auto readList = jobj.value(G_Files::ActionCursorMovsMap_KeyWord).toVariant().toList();
        CursorMovementsList actList;
        for(const auto &el : readList)
        {
            auto jarr = el.toJsonArray();
            if(jarr.size() < 4)
                continue;
            MovementList movList;
            movList << jarr[0].toInt() << jarr[1].toInt() << jarr[2].toInt() << jarr[3].toInt();
            actList.append(movList);
        }
        params.m_cursorMovementsList = actList;
        params.m_dataId = jobj.value(G_Files::ActionCursorMovsId_KeyWord).toString();
        params.m_timesToRun = jobj.value(G_Files::ActionCursorMovsLoop_KeyWord).toInt();
        params.m_cursorMovementsOptionalKeysStroke = jobj.value(G_Files::ActionCursorMovsOptKeysStroke_KeyWord).toVariant().toStringList();
    }
    else if(type == G_Files::ActionRunningOtherTaskType_Value)
    {
        actionToReturn = std::make_shared<RunningOtherTaskAction>();
        params.m_taskName = jobj.value(G_Files::RunningOtherTaskFilename_KeyWord).toString();
        params.m_delay = jobj.value(G_Files::RunningOtherTaskDelay_KeyWord).toInt();
        params.m_timesToRun = jobj.value(G_Files::RunningOtherTaskLoops_KeyWord).toInt();
    }
    else
    {
        return nullptr;
    }

    actionToReturn->setParameters(params);
    actionToReturn->optionalProcesses();

    return actionToReturn;
}

QJsonObject TaskJsonLoader::actionToJson(const std::shared_ptr<AbstractAction> &act)
{
    QJsonObject jsonToReturn;

    switch(act->e_type)
    {
        case ActionType::Paste:
        {
            jsonToReturn.insert(G_Files::ActionType_KeyWord, QJsonValue::fromVariant(G_Files::ActionPasteType_Value));
            auto pasteaction = dynamic_cast<PasteAction*>(act.get());
            if(pasteaction != nullptr)
            {
                auto params = pasteaction->generateParameters();
                jsonToReturn.insert(G_Files::ActionContent_KeyWord, QJsonValue::fromVariant(params.m_pasteContent));
                jsonToReturn.insert(G_Files::ActionContentId_KeyWord, QJsonValue::fromVariant(params.m_dataId));
                jsonToReturn.insert(G_Files::ActionPasteTextLoop_KeyWord, QJsonValue::fromVariant(params.m_timesToRun));
            }
        }
        break;
        case ActionType::Wait:
        {
            jsonToReturn.insert(G_Files::ActionType_KeyWord, QJsonValue::fromVariant(G_Files::ActionWaitType_Value));
            auto waitaction = dynamic_cast<WaitAction*>(act.get());
            if(waitaction != nullptr)
            {
                auto params = waitaction->generateParameters();
                jsonToReturn.insert(G_Files::ActionDuration_KeyWord, QJsonValue::fromVariant((double)params.m_waitDuration));
            }
        }
        break;
        case ActionType::KeysSequence:
        {
            jsonToReturn.insert(G_Files::ActionType_KeyWord, QJsonValue::fromVariant(G_Files::ActionKeysSequenceType_Value));
            auto keySeqaction = dynamic_cast<KeysSequenceAction*>(act.get());
            if(keySeqaction != nullptr)
            {
                auto params = keySeqaction->generateParameters();
                QJsonObject writtenMapJObj;
                for(const auto &[key, val] : params.m_keysSeqMap.asKeyValueRange())
                {
                    QJsonArray jarr;
                    jarr.append(val.first);
                    jarr.append(QJsonValue::fromVariant(val.second));
                    writtenMapJObj.insert(QString::number(key), jarr);
                }
                jsonToReturn.insert(G_Files::ActionKeysSeqMap_KeyWord, writtenMapJObj);
                jsonToReturn.insert(G_Files::ActionKeysSeqId_KeyWord, QJsonValue::fromVariant(params.m_dataId));
                jsonToReturn.insert(G_Files::ActionKeysSeqLoop_KeyWord, QJsonValue::fromVariant(params.m_timesToRun));
            }
        }
        break;
        case ActionType::SystemCommand:
        {
            jsonToReturn.insert(G_Files::ActionType_KeyWord, QJsonValue::fromVariant(G_Files::ActionSystemCommandeType_Value));
            auto sysCmdaction = dynamic_cast<SystemCommandAction*>(act.get());
            if(sysCmdaction != nullptr)
            {
                auto params = sysCmdaction->generateParameters();
                jsonToReturn.insert(G_Files::ActionSysCommandType_KeyWord, QJsonValue::fromVariant(params.m_sysCmdTypeStr));
                jsonToReturn.insert(G_Files::ActionSysCommandParam1_KeyWord, QJsonValue::fromVariant(params.m_sysCmdParam1));
                jsonToReturn.insert(G_Files::ActionSysCommandParam2_KeyWord, QJsonValue::fromVariant(params.m_sysCmdParam2));
            }
        }
        break;
        case ActionType::CursorMovements:
        {
            jsonToReturn.insert(G_Files::ActionType_KeyWord, QJsonValue::fromVariant(G_Files::ActionCursorMovementsType_Value));
            auto cursorMovsaction = dynamic_cast<CursorMovementsAction*>(act.get());
            if(cursorMovsaction != nullptr)
            {
                auto params = cursorMovsaction->generateParameters();
                QJsonArray writtenListJArr;
                for(const auto &mov : params.m_cursorMovementsList)
                {
                    if(mov.size() < 4)
                        continue;
                    QJsonArray jarr;
                    jarr.append(mov[0]);
                    jarr.append(mov[1]);
                    jarr.append(mov[2]);
                    jarr.append(mov[3]);
                    writtenListJArr.append(jarr);
                }
                jsonToReturn.insert(G_Files::ActionCursorMovsMap_KeyWord, writtenListJArr);
                jsonToReturn.insert(G_Files::ActionCursorMovsId_KeyWord, QJsonValue::fromVariant(params.m_dataId));
                jsonToReturn.insert(G_Files::ActionCursorMovsLoop_KeyWord, QJsonValue::fromVariant(params.m_timesToRun));
                jsonToReturn.insert(G_Files::ActionCursorMovsOptKeysStroke_KeyWord, QJsonValue::fromVariant(params.m_cursorMovementsOptionalKeysStroke));
            }
        }
        break;
        case ActionType::RunningOtherTask:
        {
            jsonToReturn.insert(G_Files::ActionType_KeyWord, QJsonValue::fromVariant(G_Files::ActionRunningOtherTaskType_Value));
            auto runOtherTaskaction = dynamic_cast<RunningOtherTaskAction*>(act.get());
            if(runOtherTaskaction != nullptr)
            {
                auto params = runOtherTaskaction->generateParameters();
                jsonToReturn.insert(G_Files::RunningOtherTaskFilename_KeyWord, QJsonValue::fromVariant(params.m_taskName));
                jsonToReturn.insert(G_Files::RunningOtherTaskDelay_KeyWord, QJsonValue::fromVariant(params.m_delay));
                jsonToReturn.insert(G_Files::RunningOtherTaskLoops_KeyWord, QJsonValue::fromVariant(params.m_timesToRun));
            }
        }
        break;
        default:
            break;
    }

    return jsonToReturn;
}
