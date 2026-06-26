# Tasket HTTP Daemon

This is the Tasket++ HTTP sidecar used by Macrohelp. It exposes saved `.scht` tasks and temporary task creation over a local HTTP API.

The resulting `bin` folder includes `tasket-httpd.exe`, Qt DLLs, and Qt plugin folders required for Windows.

## Run a Build

Use the live Tasket++ saved task folder:

```powershell
cd tasket-http-daemon/bin
.\tasket-httpd.exe --port 7777 --bind 127.0.0.1 --dir "$env:APPDATA\Tasket++\saved_tasks" --default-delay 1
```

Use the package (after build):

```powershell
cd tasket-http-daemon/bin
.\tasket-httpd.exe --port 7777 --bind 127.0.0.1 --dir "folder\of\tasks" --default-delay 1
```

## Build From Source

```powershell
cd tasket-http-daemon/source
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
```

The resulting portable daemon can be run with:

```powershell
.\build-msvc\Release\tasket-httpd.exe
```

## Stop Commands

Stop scheduled/running Tasket actions:
> also hotkey'd under Shift+Alt+7 with overlay runtime live communicating this..

```powershell
Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:7777/stop" -Body "{}" -ContentType "application/json"
```

Stop the daemon process:

```powershell
Get-Process tasket-httpd -ErrorAction SilentlyContinue | Stop-Process -Force
```

## Endpoints

```text
GET  /                                  API info and tool inventory.
GET  /health                           Health check and task directory count.
GET  /tasks                            List available .scht macros.
GET  /run?task=X&delay=N&loop=N        Schedule a saved macro by query string.
POST /run {"task":"X","delay":1}       Schedule a saved macro by JSON body.
POST /temp-task                        Write a temporary .scht task and optionally run it.
GET  /check?id=N                       Query scheduled/running/finished state for task number N.
GET  /task?id=N                        Compatibility alias for /check.
GET  /status                           Full daemon status.
POST /stop?id=N                        Stop one task by number.
POST /stop                             Stop all active tasks.
POST /entrypoint                       Set a workflow entrypoint value.
GET  /entrypoints                      List workflow entrypoint values.
POST /grid                             Set a grid cell value.
GET  /grid                             List grid cell values.
```

Useful probes:

```powershell
Invoke-RestMethod "http://127.0.0.1:7777/health"
Invoke-RestMethod "http://127.0.0.1:7777/tasks"
Invoke-RestMethod "http://127.0.0.1:7777/status"
```

Temporary task test:

```powershell
$body = @{
  name = "macrohelp_temporary_task"
  delay = 1
  loop = 1
  run = $true
  cleanup = $true
  task = @{
    docType = "ScheduleTask File"
    version = "1.0"
    name = "macrohelp_temporary_task"
    description = "testing-123"
    actions = @(
      @{ type = "WaitAction"; timeToWaitInMs = 100 }
    )
  }
} | ConvertTo-Json -Depth 10

$created = Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:7777/temp-task" -Body $body -ContentType "application/json"
Start-Sleep -Seconds 2
Invoke-RestMethod "http://127.0.0.1:7777/check?id=$($created.task_number)"
```

