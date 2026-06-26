#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <iostream>

#include "TaskRegistry.h"
#include "TaskRunner.h"
#include "HttpServer.h"
#include "mainwindow.h"
#include "globals.h"

#ifndef APP_VERSION
#define APP_VERSION "1.8.0"
#endif

void printBanner()
{
    std::cout << R"(
  ================================================
   Tasket++ HTTP Trigger Daemon  v)" << APP_VERSION << R"(
  ================================================
   LAN-only HTTP endpoint for Tasket++ automation
   Numbered tasks | Configurable delays | Lifecycle tracking
   No cloud. No NAT. Sovereign control.
  ================================================
)" << std::endl;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Tasket++ HTTP Trigger");
    app.setApplicationVersion(APP_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription("HTTP trigger daemon for Tasket++ automation tasks");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption portOption(QStringList() << "p" << "port",
        "TCP port to listen on.", "port", "7777");
    QCommandLineOption bindOption(QStringList() << "b" << "bind",
        "Network interface to bind to.", "host", "0.0.0.0");
    QCommandLineOption dirOption(QStringList() << "d" << "dir",
        "Directory containing .scht task files.", "path", "saved_tasks");
    QCommandLineOption keyOption(QStringList() << "k" << "key",
        "Optional API key for X-Tasket-Key header auth.", "secret", "");
    QCommandLineOption delayOption(QStringList() << "D" << "default-delay",
        "Default delay in seconds before running a task.", "seconds", "10");

    parser.addOption(portOption);
    parser.addOption(bindOption);
    parser.addOption(dirOption);
    parser.addOption(keyOption);
    parser.addOption(delayOption);
    parser.process(app);

    printBanner();

    // ---- Resolve paths ----
    QString tasksDir = parser.value(dirOption);
    if(QDir::isRelativePath(tasksDir))
        tasksDir = QDir(QApplication::applicationDirPath()).absoluteFilePath(tasksDir);
    QDir().mkpath(tasksDir);

    QString bindHost = parser.value(bindOption);
    int port = parser.value(portOption).toInt();
    if(port <= 0 || port > 65535)
    {
        std::cerr << "[ERROR] Invalid port: " << port << std::endl;
        return 1;
    }
    int defaultDelay = parser.value(delayOption).toInt();
    if(defaultDelay < 0) defaultDelay = 10;

    QString apiKey = parser.value(keyOption);

    // ---- Verify task directory ----
    if(!QDir(tasksDir).exists())
    {
        std::cerr << "[ERROR] Tasks directory missing: " << tasksDir.toStdString() << std::endl;
        return 1;
    }

    QDir taskDirObj(tasksDir);
    QStringList schtFiles = taskDirObj.entryList(QStringList() << "*.scht", QDir::Files);
    std::cout << "[INFO] Scanned " << schtFiles.size() << " task(s) in " << tasksDir.toStdString() << std::endl;
    for(const QString &f : schtFiles)
    {
        std::cout << "       - " << QFileInfo(f).baseName().toStdString() << ".scht" << std::endl;
    }

    // ---- Initialize engine ----
    auto mainWindowStub = MainWindow::getInstance(); // Initialize singleton stub

    TaskRegistry registry;
    TaskRunner runner(&registry);
    HttpServer server(&registry);

    // Wire registry -> runner (when delay timer expires)
    QObject::connect(&registry, &TaskRegistry::executeRequested,
                     &runner, &TaskRunner::executeTask);

    // Wire RunningOtherTaskAction -> registry. The sidecar has no real Tasket++
    // main window, so the stub forwards "run another task" here.
    QObject::connect(mainWindowStub.get(), &MainWindow::taskRequested,
        &registry,
        [&](const QString &filename, int delay, int loopTimes) {
            QString cleanName = filename.trimmed();
            if(cleanName.endsWith(G_Files::TasksFileExtension, Qt::CaseInsensitive))
                cleanName.chop(G_Files::TasksFileExtension.length());

            QDir dir(tasksDir);
            QString taskPath;
            QFileInfo direct(dir.absoluteFilePath(cleanName + G_Files::TasksFileExtension));
            if(direct.exists())
            {
                taskPath = direct.absoluteFilePath();
            }
            else
            {
                QFileInfoList files = dir.entryInfoList(QStringList() << "*.scht", QDir::Files);
                for(const QFileInfo &f : files)
                {
                    if(f.baseName().compare(cleanName, Qt::CaseInsensitive) == 0)
                    {
                        taskPath = f.absoluteFilePath();
                        cleanName = f.baseName();
                        break;
                    }
                }
            }

            if(taskPath.isEmpty())
            {
                std::cerr << "[RUN_OTHER_FAILED] Task '" << filename.toStdString()
                          << "' not found in " << tasksDir.toStdString() << std::endl;
                return;
            }

            QString description;
            QFile file(taskPath);
            if(file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                if(doc.isObject())
                    description = doc.object().value(G_Files::DocumentTaskDescription_KeyWord).toString();
            }

            int safeDelay = delay < 0 ? 0 : delay;
            int safeLoops = loopTimes == 0 ? 1 : loopTimes;
            TaskInstance ti = registry.scheduleTask(cleanName, taskPath, safeDelay, safeLoops, description);
            std::cout << "[RUN_OTHER] Task #" << ti.taskNumber << " '"
                      << cleanName.toStdString() << "' scheduled by parent in "
                      << safeDelay << "s" << std::endl;
        },
        Qt::QueuedConnection);

    // Wire lifecycle logging
    QObject::connect(&registry, &TaskRegistry::taskFinished,
        [](int num, QString name) {
            std::cout << "[FINISHED] Task #" << num << " '" << name.toStdString()
                      << "' has finished" << std::endl;
        });

    QObject::connect(&registry, &TaskRegistry::taskFailed,
        [](int num, QString name, QString error) {
            std::cerr << "[FAILED] Task #" << num << " '" << name.toStdString()
                      << "' failed: " << error.toStdString() << std::endl;
        });

    QObject::connect(&server, &HttpServer::requestReceived,
        [](const QString &method, const QString &path, const QString &details) {
            std::cout << "[HTTP] " << method.toStdString() << " " << path.toStdString()
                      << " | " << details.toStdString() << std::endl;
        });

    // Configure and start HTTP server
    server.setDefaultDelay(defaultDelay);
    if(!server.start(bindHost, port, tasksDir, apiKey))
    {
        std::cerr << "[ERROR] Failed to start HTTP server on " << bindHost.toStdString()
                  << ":" << port << std::endl;
        return 1;
    }

    std::cout << "[READY] Server: " << server.listenAddress().toStdString() << std::endl;
    std::cout << "[CONFIG] Tasks: " << tasksDir.toStdString() << std::endl;
    std::cout << "[CONFIG] Default delay: " << defaultDelay << "s" << std::endl;
    if(!apiKey.isEmpty())
        std::cout << "[SECURITY] API key authentication enabled (X-Tasket-Key header)" << std::endl;
    else
        std::cout << "[SECURITY] No API key — LAN-only binding recommended" << std::endl;

    std::cout << R"(
Endpoints:
  GET  /                       API info + tool inventory
  GET  /health                 Health check
  GET  /tasks                  List available macros with delays
  GET  /run?task=X&delay=N     Schedule macro (delay overrides default)
  POST /run {"task":"X"}       Schedule macro via JSON
  GET  /check?id=N             Query task status & remaining time
  GET  /status                 Full daemon status
  POST /stop?id=N              Stop a specific task
  POST /stop                   Stop all tasks
  POST /entrypoint             Set workflow entrypoint value
  GET  /entrypoints            List all entrypoint values
  POST /grid                   Set data grid cell value
  GET  /grid                   List all grid cell values
)" << std::endl;

    // Graceful shutdown
    QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
        std::cout << "[SHUTDOWN] Stopping daemon..." << std::endl;
        server.stop();
        runner.stopAllTasks();
    });

    return app.exec();
}
