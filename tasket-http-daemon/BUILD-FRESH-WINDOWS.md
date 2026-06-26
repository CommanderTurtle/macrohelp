# Fresh Windows Qt6 Build Quick Start

This is the rebuild path for `tasket-httpd.exe` on a clean Windows workstation. It intentionally builds with Qt6 only, disables common build-tool telemetry switches for the session, installs Qt into a disposable cache folder, deploys the runtime DLLs and Qt plugin folders beside the EXE, runs a daemon health check, and can remove the temporary Qt SDK afterward.

The current daemon uses `cpp-httplib` for HTTP, but it still needs Qt because it embeds the Tasket++ action engine. Qt is the task engine/runtime dependency, not the HTTP dependency.

## Expected Machine State

- Windows 11.
- PowerShell 7 preferred, Windows PowerShell also works for the script.
- Visual Studio 2026 or Visual Studio Build Tools 2026 with Desktop C++ workload.
- CMake 4.2+ for the `Visual Studio 18 2026` generator. If using Visual Studio 2022, CMake 3.16+ is enough.
- Git.
- `uv`, used to run `aqtinstall` without permanently installing Python packages.

Recommended winget installs for a new builder:

```powershell
winget install --source winget --id Git.Git
winget install --source winget --id Kitware.CMake
winget install --source winget --id astral-sh.uv
```

Install Visual Studio 2026 from the Visual Studio Installer and include:

- Desktop development with C++.
- MSVC v145 or newer x64/x86 build tools.
- Windows 11 SDK.
- C++ CMake tools for Windows.

## One-Shot Dry Run

From this folder:

```powershell
pwsh .\build.ps1 -DryRun
```

By default, `build.ps1` reads the pinned Qt version from `QT_VERSION.txt`.

This prints:

- Tool availability.
- Selected Visual Studio CMake generator.
- Latest installable Qt6 version resolved by `aqt` for `win64_msvc2022_64`.
- Qt architecture availability.
- The exact configure/build/deploy commands.
- The cleanup target that would be removed if `-CleanQtAfterBuild` is used.

## Build With Temporary Qt6

```powershell
pwsh .\build.ps1
```

The script installs Qt under:

```text
source\_qt6-build-cache
```

It writes a sentinel file into that folder and only removes that folder when the sentinel exists. It does not delete arbitrary `C:\Qt` installs.

## Pinned Qt Build

For repeatability, pin a known-good Qt version:

```powershell
pwsh .\build.ps1 -QtVersion 6.8.4
```

For newest installable Qt6 on the current Qt mirror:

```powershell
pwsh .\build.ps1 -QtVersion latest-compatible
```

For newest advertised Qt6, even if the mirror may not expose a working MSVC package yet:

```powershell
pwsh .\build.ps1 -QtVersion latest
```

To fail if `aqt` advertises a newer Qt than the newest installable Qt for the selected architecture:

```powershell
pwsh .\build.ps1 -RequireNewestQt
```

## Manual Build Equivalent

```powershell
$env:DOTNET_CLI_TELEMETRY_OPTOUT = "1"
$env:POWERSHELL_TELEMETRY_OPTOUT = "1"
$env:POWERSHELL_UPDATECHECK = "Off"
$env:VCPKG_DISABLE_METRICS = "1"
$env:VSCMD_SKIP_SENDTELEMETRY = "1"

uvx --from aqtinstall aqt list-qt windows desktop --spec ">=6" --latest-version
uvx --from aqtinstall aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O .\_qt6-build-cache --archives qtbase

git clone https://github.com/AmirHammouteneEI/ScheduledPasteAndKeys.git original
git -C original apply ..\patches\Task.h.patch

cmake -S . -B build-msvc -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_PREFIX_PATH="$PWD\_qt6-build-cache\<version>\msvc2022_64" `
  -DTASKETPP_ROOT="$PWD\original" `
  -DTASKET_HTTP_BIND=127.0.0.1

cmake --build build-msvc --config Release --parallel
& "$PWD\_qt6-build-cache\<version>\msvc2022_64\bin\windeployqt.exe" "$PWD\build-msvc\Release\tasket-httpd.exe"
pwsh .\scripts\verify-runtime-layout.ps1 -BinDir .\build-msvc\Release
```

`verify-runtime-layout.ps1` requires the EXE, Qt DLLs, and `platforms\qwindows.dll`. If that file is missing, the daemon is not a standalone runtime folder; it is borrowing Qt from the current machine.

Run:

```powershell
.\build-msvc\Release\tasket-httpd.exe --port 7777 --bind 127.0.0.1 --dir "$env:APPDATA\Tasket++\saved_tasks" --default-delay 1
```

Stop:

```powershell
Get-Process tasket-httpd -ErrorAction SilentlyContinue | Stop-Process -Force
```

Nuke only the disposable Qt cache:

```powershell
Remove-Item -LiteralPath .\_qt6-build-cache -Recurse -Force
```

## Winget Telemetry Note

For winget itself, run:

```powershell
winget settings
```

Then set:

```json
{
  "telemetry": {
    "disable": true
  }
}
```

The build script also sets session-level telemetry opt-out variables for PowerShell, .NET, vcpkg, and Visual Studio command environment.
