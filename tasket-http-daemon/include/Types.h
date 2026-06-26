#ifndef TYPES_H
#define TYPES_H

#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

// ---------------------------------------------------------------------------
// Strongly-typed enumerations for the entire HTTP trigger system
// ---------------------------------------------------------------------------

enum class TaskState {
    Idle,       // Task record created but not yet queued
    Scheduled,  // Waiting for delay timer to expire
    Running,    // TaskThread is executing actions
    Finished,   // All actions completed successfully
    Stopped,    // User requested stop
    Failed      // Error during load or execution
};

inline QString taskStateToString(TaskState s) {
    switch(s) {
        case TaskState::Idle:       return "idle";
        case TaskState::Scheduled:  return "scheduled";
        case TaskState::Running:    return "running";
        case TaskState::Finished:   return "finished";
        case TaskState::Stopped:    return "stopped";
        case TaskState::Failed:     return "failed";
    }
    return "unknown";
}

enum class HttpStatusCode : int {
    Ok                  = 200,
    Created             = 201,
    Accepted            = 202,
    BadRequest          = 400,
    Unauthorized        = 401,
    Forbidden           = 403,
    NotFound            = 404,
    Conflict            = 409,
    InternalError       = 500,
    NotImplemented      = 501,
    ServiceUnavailable  = 503
};

enum class ActionTypeName {
    Paste,
    Wait,
    KeysSequence,
    SystemCommand,
    CursorMovements,
    RunningOtherTask,
    Unknown
};

inline QString actionTypeToString(ActionTypeName a) {
    switch(a) {
        case ActionTypeName::Paste:             return "paste";
        case ActionTypeName::Wait:              return "wait";
        case ActionTypeName::KeysSequence:      return "keyssequence";
        case ActionTypeName::SystemCommand:     return "systemcommand";
        case ActionTypeName::CursorMovements:   return "cursormovements";
        case ActionTypeName::RunningOtherTask:  return "runningothertask";
        case ActionTypeName::Unknown:           return "unknown";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Structured data types
// ---------------------------------------------------------------------------

/**
 * @brief Represents a single scheduled/running/finished task instance.
 * Each invocation of /run creates a new TaskInstance with a unique number.
 */
struct TaskInstance {
    int             taskNumber = 0;         // Monotonically increasing ID
    QString         taskName;               // Base name of the .scht file
    QString         taskPath;               // Absolute path to .scht
    TaskState       state = TaskState::Idle;
    int             delaySeconds = 10;      // Configured delay before running
    int             loopTimes = 1;          // Loop count (-1 = infinite)
    QDateTime       createdAt;              // When the /run request was received
    QDateTime       scheduledToRunAt;       // createdAt + delaySeconds
    QDateTime       startedAt;              // When TaskThread actually began
    QDateTime       finishedAt;             // When execution completed
    QString         errorMessage;           // Populated if state == Failed
    QString         description;            // From .scht "description" field

    /**
     * @brief Serialize to JSON for API responses.
     */
    QJsonObject toJson(bool verbose = false) const {
        QJsonObject j;
        j["task_number"]    = taskNumber;
        j["name"]           = taskName;
        j["state"]          = taskStateToString(state);
        j["delay_seconds"]  = delaySeconds;
        j["loop_times"]     = loopTimes;
        j["created_at"]     = createdAt.toString(Qt::ISODate);
        j["scheduled_at"]   = scheduledToRunAt.toString(Qt::ISODate);

        if(state == TaskState::Scheduled) {
            qint64 remainingMs = QDateTime::currentDateTime().msecsTo(scheduledToRunAt);
            j["remaining_seconds"] = remainingMs > 0 ? static_cast<int>(remainingMs / 1000) : 0;
        }
        if(verbose) {
            j["started_at"]     = startedAt.isValid() ? startedAt.toString(Qt::ISODate) : QString();
            j["finished_at"]    = finishedAt.isValid() ? finishedAt.toString(Qt::ISODate) : QString();
            j["description"]    = description;
            if(!errorMessage.isEmpty())
                j["error"] = errorMessage;
        }
        return j;
    }

    /**
     * @brief Human-readable status message for /check endpoint.
     */
    QString checkMessage() const {
        switch(state) {
            case TaskState::Scheduled: {
                qint64 remainingSec = QDateTime::currentDateTime().secsTo(scheduledToRunAt);
                if(remainingSec < 0) remainingSec = 0;
                return QString("'%1' is scheduled to run in ~%2s")
                    .arg(taskName).arg(remainingSec);
            }
            case TaskState::Running: {
                qint64 elapsedSec = startedAt.secsTo(QDateTime::currentDateTime());
                return QString("'%1' is currently running (elapsed %2s)")
                    .arg(taskName).arg(elapsedSec);
            }
            case TaskState::Finished:
                return QString("'%1' has finished").arg(taskName);
            case TaskState::Stopped:
                return QString("'%1' was stopped before completion").arg(taskName);
            case TaskState::Failed:
                return QString("'%1' failed: %2").arg(taskName).arg(errorMessage);
            case TaskState::Idle:
                return QString("'%1' is idle").arg(taskName);
        }
        return QString("'%1' status unknown").arg(taskName);
    }
};

/**
 * @brief Typed API response builder for consistent JSON output.
 */
struct ApiResponse {
    HttpStatusCode  status = HttpStatusCode::Ok;
    bool            success = true;
    QString         message;
    QJsonObject     data;

    static ApiResponse ok(const QString &msg = QString()) {
        ApiResponse r;
        r.success = true;
        r.message = msg;
        return r;
    }

    static ApiResponse error(HttpStatusCode code, const QString &msg) {
        ApiResponse r;
        r.status  = code;
        r.success = false;
        r.message = msg;
        return r;
    }

    static ApiResponse taskScheduled(const TaskInstance &task) {
        ApiResponse r;
        r.success = true;
        r.message = QString("'%1' scheduled to run in %2s : success")
                        .arg(task.taskName).arg(task.delaySeconds);
        r.data = task.toJson();
        return r;
    }

    static ApiResponse taskFinished(const TaskInstance &task) {
        ApiResponse r;
        r.success = true;
        r.message = QString("'%1' has finished").arg(task.taskName);
        r.data = task.toJson();
        return r;
    }

    static ApiResponse taskCheck(const TaskInstance &task) {
        ApiResponse r;
        r.success = true;
        r.message = task.checkMessage();
        r.data = task.toJson(true); // verbose
        return r;
    }

    QByteArray toJsonBytes() const {
        QJsonObject root;
        root["success"] = success;
        root["message"] = message;
        if(!data.isEmpty())
            root["data"] = data;
        return QJsonDocument(root).toJson(QJsonDocument::Indented);
    }
};

/**
 * @brief Descriptor for available tools/actions shown on GET /
 */
struct ToolDescriptor {
    QString     name;
    QString     description;
    QString     endpoint;
    QString     method;
    QString     example;
    QJsonObject params;

    QJsonObject toJson() const {
        QJsonObject j;
        j["name"]        = name;
        j["description"] = description;
        j["endpoint"]    = endpoint;
        j["method"]      = method;
        j["example"]     = example;
        if(!params.isEmpty())
            j["parameters"] = params;
        return j;
    }
};

#endif // TYPES_H
