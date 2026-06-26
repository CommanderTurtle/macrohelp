#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include "Types.h"
#include "TaskRegistry.h"

#include <QObject>
#include <QThread>
#include <QString>
#include <QJsonObject>
#include <QMap>
#include <QMutex>

namespace httplib {
class Server;
struct Request;
struct Response;
}

/**
 * @brief Fully-typed HTTP daemon for the Tasket++ trigger system.
 *
 * All endpoints return strongly-typed JSON via ApiResponse.
 * Every /run invocation assigns a task number and schedules with
 * a configurable delay (default 10s, overridable per task and per request).
 *
 * Endpoints:
 *   GET  /              -> Full tool inventory
 *   GET  /health        -> Daemon health
 *   GET  /tasks         -> List available .scht macros
 *   GET  /run           -> Schedule task by query param
 *   POST /run           -> Schedule task by JSON body
 *   GET  /check?id=N    -> Check task status by number
 *   GET  /status        -> Global status + active tasks
 *   POST /stop          -> Stop all tasks
 *   POST /stop?id=N     -> Stop specific task
 *   POST /entrypoint    -> Set entrypoint value
 *   GET  /entrypoints   -> List all entrypoint values
 *   POST /grid          -> Set grid cell value
 *   GET  /grid          -> List all grid cell values
 */
class HttpServer : public QObject
{
    Q_OBJECT

public:
    explicit HttpServer(TaskRegistry *registry, QObject *parent = nullptr);
    ~HttpServer();

    bool start(const QString &host, int port, const QString &tasksDir,
               const QString &apiKey = QString());
    void stop();
    bool isRunning() const;

    /** Set the default delay before running a task (seconds). Default: 10. */
    void setDefaultDelay(int seconds) { m_defaultDelay = seconds; }

    QString listenAddress() const;

signals:
    void requestReceived(const QString &method, const QString &path,
                         const QString &details);

private:
    void setupRoutes();
    bool checkAuth(const httplib::Request &req, httplib::Response &res) const;

    // --- Typed route handlers ---
    void handleRoot          (const httplib::Request &req, httplib::Response &res);
    void handleHealth        (const httplib::Request &req, httplib::Response &res);
    void handleListTasks     (const httplib::Request &req, httplib::Response &res);
    void handleRunGet        (const httplib::Request &req, httplib::Response &res);
    void handleRunPost       (const httplib::Request &req, httplib::Response &res);
    void handleTempTaskPost  (const httplib::Request &req, httplib::Response &res);
    void handleCheck         (const httplib::Request &req, httplib::Response &res);
    void handleStatus        (const httplib::Request &req, httplib::Response &res);
    void handleStop          (const httplib::Request &req, httplib::Response &res);
    void handleSetEntrypoint (const httplib::Request &req, httplib::Response &res);
    void handleListEntrypoints(const httplib::Request &req, httplib::Response &res);
    void handleSetGrid       (const httplib::Request &req, httplib::Response &res);
    void handleListGrid      (const httplib::Request &req, httplib::Response &res);

    // --- Helpers ---
    void sendResponse(httplib::Response &res, const ApiResponse &api) const;
    QStringList scanTaskNames() const;
    QString resolveTaskPath(const QString &taskName) const;
    QString safeTempTaskName(const QString &taskName) const;
    bool writeTaskFile(const QString &taskName, const QJsonObject &task, QString *writtenPath, QString *error) const;
    void rememberTempTaskFile(int taskNumber, const QString &path);
    void cleanupTempTaskFile(int taskNumber);
    void cleanupAllTempTaskFiles();
    int readTaskDelay(const QString &taskPath, int defaultDelay) const;
    QString readTaskDescription(const QString &taskPath) const;
    QList<ToolDescriptor> buildToolInventory() const;

    TaskRegistry *m_registry = nullptr;
    QString       m_tasksDir;
    QString       m_apiKey;
    QString       m_host;
    int           m_port = 0;
    int           m_defaultDelay = 10;

    // In-memory data stores (entrypoints + grid) for workflow integration
    mutable QMutex        m_dataMutex;
    QMap<QString, QString> m_entrypoints; // id -> value
    QMap<QString, QString> m_gridCells;   // cell_id -> value

    mutable QMutex      m_tempCleanupMutex;
    QMap<int, QString>  m_tempTaskFiles;  // task number -> generated temp .scht path

    QThread      *m_serverThread = nullptr;
    httplib::Server *m_server = nullptr;
    bool          m_running = false;
};

#endif // HTTPSERVER_H
