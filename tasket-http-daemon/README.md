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

## Build From Source with the:

# Tasket HTTPD Minimal Source Builder

This folder is the minimal source-first builder for `tasket-httpd.exe`.

Qt is pinned by default in:

```text
QT_VERSION.txt
```

The current pin is `6.10.3`. The build script still checks the Qt feed during dry runs/builds and warns if newer Qt versions are advertised.

It intentionally does not include:

- A built EXE.
- Qt DLLs.
- Qt plugin folders.
- A Qt SDK.
- A cloned Tasket++ `original` tree.
- A CMake build folder.

On a fresh machine, run:

```powershell
pwsh .\build.ps1 -DryRun
pwsh .\build.ps1
```

That performs the full source route:

1. Sets session telemetry opt-outs.
2. Checks for `git`, `cmake`, and `uv`.
3. Selects Visual Studio 2026 if CMake supports it, otherwise Visual Studio 2022.
4. Uses the pinned Qt version from `QT_VERSION.txt` unless `-QtVersion` is supplied.
5. Installs only Qt `qtbase` into `_qt6-build-cache`.
6. Clones Tasket++ source into `original` if no sibling/local `original` exists.
7. Applies `patches\Task.h.patch`.
8. Configures and builds with CMake.
9. Runs `windeployqt`.
10. Verifies the standalone runtime layout, including `platforms\qwindows.dll`.
11. Runs `/health` and throws if the daemon is not healthy.
12. Removes only the sentinel-owned `_qt6-build-cache` when `-CleanQtAfterBuild` is supplied.

To hard-fail when the Qt feed advertises a newer version than the pinned build:

```powershell
pwsh .\build.ps1 -RequireNewestQt
```

To intentionally ignore the pin and use the newest installable Qt6:

```powershell
pwsh .\build.ps1 -QtVersion latest-compatible
```

To force a completely fresh source rebuild after a previous run, delete:

```powershell
Remove-Item .\build-msvc -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item .\original -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item .\_qt6-build-cache -Recurse -Force -ErrorAction SilentlyContinue
```

Then rerun the build command.

For details, see `BUILD-FRESH-WINDOWS.md`.

---

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


