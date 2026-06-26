[CmdletBinding()]
param(
    [string]$QtVersion = "latest-compatible",
    [string]$QtArch = "win64_msvc2022_64",
    [string]$QtCacheRoot = "",
    [string]$TasketRepo = "https://github.com/AmirHammouteneEI/ScheduledPasteAndKeys.git",
    [string]$BuildDir = "build-msvc",
    [switch]$DryRun,
    [switch]$InstallQt,
    [switch]$Build,
    [switch]$HealthCheck,
    [switch]$RequireNewestQt,
    [switch]$CleanQtAfterBuild
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Require-Command([string]$Name, [string]$InstallHint) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $cmd) {
        Write-Warning "$Name was not found. $InstallHint"
        return $false
    }
    Write-Host "ok: $Name -> $($cmd.Source)"
    return $true
}

function Get-CMakeGenerator {
    $help = (& cmake --help) -join "`n"
    if ($help -match "Visual Studio 18 2026") { return "Visual Studio 18 2026" }
    if ($help -match "Visual Studio 17 2022") { return "Visual Studio 17 2022" }
    throw "No supported Visual Studio CMake generator found. Install VS 2026/2022 with Desktop C++ and a matching CMake."
}

function Resolve-QtVersion([string]$Requested) {
    if ($Requested -ne "latest" -and $Requested -ne "latest-compatible") { return $Requested }

    $versionsOutput = & uvx --from aqtinstall aqt list-qt windows desktop --spec ">=6" 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Could not list Qt6 versions with aqt. Output: $($versionsOutput -join "`n")"
    }

    $versions = ($versionsOutput -join " " -split "\s+") |
        ForEach-Object { $_.ToString().Trim() } |
        Where-Object { $_ -match "^\d+\.\d+\.\d+$" } |
        Sort-Object { [version]$_ } -Descending

    if (-not $versions) {
        throw "aqt returned no Qt6 versions."
    }

    $script:NewestAdvertisedQt = $versions[0]

    if ($Requested -eq "latest") {
        return $versions[0]
    }

    $skipped = @()
    foreach ($candidate in $versions) {
        $probeRoot = Join-Path ([System.IO.Path]::GetTempPath()) "macrohelp-aqt-probe"
        $probeOutput = & uvx --from aqtinstall aqt install-qt windows desktop $candidate $script:QtArch -O $probeRoot --archives qtbase --dry-run 2>&1
        if ($LASTEXITCODE -eq 0) {
            $script:SkippedNewerQt = $skipped
            return $candidate
        } else {
            $skipped += $candidate
            Write-Warning "Skipping Qt $candidate; install dry-run failed for $script:QtArch."
            continue
        }
    }

    throw "Could not find an installable Qt6 version with architecture '$script:QtArch'."
}

function Find-QtPrefix([string]$Root, [string]$Arch) {
    $archFolder = $Arch -replace "^win64_", ""
    $deploy = Get-ChildItem -LiteralPath $Root -Recurse -Filter windeployqt.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\$Arch\bin\windeployqt.exe" -or $_.FullName -like "*\$archFolder\bin\windeployqt.exe" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $deploy) {
        $deploy = Get-ChildItem -LiteralPath $Root -Recurse -Filter windeployqt.exe -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
    }
    if ($deploy) {
        return (Split-Path -Parent (Split-Path -Parent $deploy.FullName))
    }

    $config = Get-ChildItem -LiteralPath $Root -Recurse -Filter Qt6Config.cmake -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\$Arch\lib\cmake\Qt6\Qt6Config.cmake" -or $_.FullName -like "*\$archFolder\lib\cmake\Qt6\Qt6Config.cmake" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $config) {
        $config = Get-ChildItem -LiteralPath $Root -Recurse -Filter Qt6Config.cmake -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
    }
    if ($config) {
        return (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $config.FullName))))
    }

    throw "Could not find a usable Qt prefix for architecture '$Arch' under '$Root'. Expected bin\windeployqt.exe or lib\cmake\Qt6\Qt6Config.cmake."
}

function Test-RuntimeLayout([string]$ExePath) {
    $bin = Split-Path -Parent $ExePath
    $required = @(
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6Network.dll",
        "Qt6Widgets.dll",
        "platforms\qwindows.dll"
    )
    $missing = @()
    foreach ($item in $required) {
        $path = Join-Path $bin $item
        if (-not (Test-Path -LiteralPath $path)) {
            $missing += $item
        }
    }
    if ($missing.Count -gt 0) {
        throw "Runtime layout is not standalone. Missing: $($missing -join ', '). Re-run windeployqt from the same Qt prefix used to build."
    }
    Write-Host "ok: runtime layout includes Qt DLLs and platforms\qwindows.dll"
}

$SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$script:NewestAdvertisedQt = $null
$script:SkippedNewerQt = @()
if (-not $QtCacheRoot) {
    $QtCacheRoot = Join-Path $SourceRoot "_qt6-build-cache"
}
$QtCacheRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($QtCacheRoot)
$Sentinel = Join-Path $QtCacheRoot ".macrohelp-owned-qt-cache"
$BuildPath = Join-Path $SourceRoot $BuildDir
$SiblingTasket = Join-Path (Split-Path -Parent $SourceRoot) "original"
$LocalTasket = Join-Path $SourceRoot "original"
if (Test-Path -LiteralPath $SiblingTasket) {
    $OriginalTasket = $SiblingTasket
} else {
    $OriginalTasket = $LocalTasket
}
$PatchPath = Join-Path $SourceRoot "patches\Task.h.patch"

Write-Step "Set session telemetry opt-outs"
$env:DOTNET_CLI_TELEMETRY_OPTOUT = "1"
$env:POWERSHELL_TELEMETRY_OPTOUT = "1"
$env:POWERSHELL_UPDATECHECK = "Off"
$env:VCPKG_DISABLE_METRICS = "1"
$env:VSCMD_SKIP_SENDTELEMETRY = "1"
Write-Host "ok: DOTNET_CLI_TELEMETRY_OPTOUT=1"
Write-Host "ok: POWERSHELL_TELEMETRY_OPTOUT=1"
Write-Host "ok: POWERSHELL_UPDATECHECK=Off"
Write-Host "ok: VCPKG_DISABLE_METRICS=1"
Write-Host "ok: VSCMD_SKIP_SENDTELEMETRY=1"

Write-Step "Check build tools"
$toolsOk = $true
$toolsOk = (Require-Command git "winget install --source winget --id Git.Git") -and $toolsOk
$toolsOk = (Require-Command cmake "winget install --source winget --id Kitware.CMake") -and $toolsOk
$toolsOk = (Require-Command uv "winget install --source winget --id astral-sh.uv") -and $toolsOk
if (-not $toolsOk) {
    throw "Missing required tools. Install the warnings above, then rerun."
}

$Generator = Get-CMakeGenerator
Write-Host "ok: selected CMake generator: $Generator"
if ($Generator -eq "Visual Studio 18 2026") {
    $cmakeVersionText = (& cmake --version | Select-Object -First 1).ToString()
    Write-Host "note: VS 2026 generator requires CMake 4.2+; detected $cmakeVersionText"
}

Write-Step "Resolve Qt"
$RequestedQtVersion = $QtVersion
$ResolvedQtVersion = Resolve-QtVersion $QtVersion
$LatestInstallableQt = $null
$LatestAdvertisedQt = $script:NewestAdvertisedQt
$SkippedNewerQt = $script:SkippedNewerQt

if ($RequestedQtVersion -ne "latest" -and $RequestedQtVersion -ne "latest-compatible") {
    $LatestInstallableQt = Resolve-QtVersion "latest-compatible"
    $LatestAdvertisedQt = $script:NewestAdvertisedQt
    $SkippedNewerQt = $script:SkippedNewerQt
}

Write-Host "ok: Qt request '$QtVersion' resolved to '$ResolvedQtVersion'"

if ($LatestInstallableQt -and ([version]$LatestInstallableQt -gt [version]$ResolvedQtVersion)) {
    $message = "Pinned Qt $ResolvedQtVersion is older than newest installable Qt $LatestInstallableQt for $QtArch."
    if ($RequireNewestQt) {
        throw $message
    }
    Write-Warning $message
} elseif ($LatestAdvertisedQt -and ([version]$LatestAdvertisedQt -gt [version]$ResolvedQtVersion)) {
    $message = "Newer Qt advertised by aqt: $LatestAdvertisedQt; pinned/selected Qt is $ResolvedQtVersion. Newer advertised versions not currently selected/installable for ${QtArch}: $($SkippedNewerQt -join ', ')"
    if ($RequireNewestQt) {
        throw $message
    }
    Write-Warning $message
} elseif ($script:NewestAdvertisedQt -and $script:NewestAdvertisedQt -ne $ResolvedQtVersion) {
    $message = "Newer Qt advertised by aqt: $script:NewestAdvertisedQt; selected newest installable $ResolvedQtVersion for $QtArch. Skipped: $($script:SkippedNewerQt -join ', ')"
    if ($RequireNewestQt) {
        throw $message
    }
    Write-Warning $message
}
Write-Host "ok: Qt architecture: $QtArch"
Write-Host "ok: disposable Qt cache: $QtCacheRoot"

$installCommand = @(
    "uvx", "--from", "aqtinstall", "aqt", "install-qt",
    "windows", "desktop", $ResolvedQtVersion, $QtArch,
    "-O", "`"$QtCacheRoot`"",
    "--archives", "qtbase"
) -join " "
Write-Host "planned Qt install:"
Write-Host "  $installCommand"

if ($DryRun) {
    Write-Step "aqt dry-run preview"
    & uvx --from aqtinstall aqt install-qt windows desktop $ResolvedQtVersion $QtArch -O $QtCacheRoot --archives qtbase --dry-run
    if ($LASTEXITCODE -ne 0) { throw "aqt dry-run failed for Qt $ResolvedQtVersion / $QtArch." }
}

if ($InstallQt) {
    Write-Step "Install temporary Qt6"
    New-Item -ItemType Directory -Force -Path $QtCacheRoot | Out-Null
    Set-Content -LiteralPath $Sentinel -Value "This Qt cache is owned by build-fresh-qt6.ps1 and may be deleted by -CleanQtAfterBuild." -Encoding UTF8
    & uvx --from aqtinstall aqt install-qt windows desktop $ResolvedQtVersion $QtArch -O $QtCacheRoot --archives qtbase
    if ($LASTEXITCODE -ne 0) { throw "aqt Qt install failed." }
}

if (-not (Test-Path -LiteralPath $QtCacheRoot)) {
    Write-Warning "Qt cache folder does not exist yet. Use -InstallQt, or pass -QtCacheRoot to an existing Qt root."
}

$QtPrefix = $null
if (Test-Path -LiteralPath $QtCacheRoot) {
    $QtPrefix = Find-QtPrefix $QtCacheRoot $QtArch
    Write-Host "ok: Qt prefix: $QtPrefix"
}

Write-Step "Prepare Tasket++ source"
if (-not (Test-Path -LiteralPath $OriginalTasket)) {
    Write-Host "planned clone: git clone $TasketRepo `"$OriginalTasket`""
    if (-not $DryRun) {
        & git clone $TasketRepo $OriginalTasket
        if ($LASTEXITCODE -ne 0) { throw "Tasket++ clone failed." }
    }
} else {
    Write-Host "ok: Tasket++ source already exists: $OriginalTasket"
}

if (Test-Path -LiteralPath $OriginalTasket) {
    $patchCheck = & git -C $OriginalTasket apply --reverse --check $PatchPath 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "ok: Task.h patch already applied."
    } else {
        Write-Host "planned patch: git -C `"$OriginalTasket`" apply `"$PatchPath`""
        if (-not $DryRun) {
            & git -C $OriginalTasket apply $PatchPath
            if ($LASTEXITCODE -ne 0) { throw "Task.h patch failed." }
        }
    }
}

if ($Build) {
    if (-not $QtPrefix) { throw "Qt prefix not found. Use -InstallQt or provide a populated -QtCacheRoot." }

    Write-Step "Configure and build"
    & cmake -S $SourceRoot -B $BuildPath -G $Generator -A x64 `
        -DCMAKE_PREFIX_PATH="$QtPrefix" `
        -DTASKETPP_ROOT="$OriginalTasket" `
        -DTASKET_HTTP_BIND=127.0.0.1
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

    & cmake --build $BuildPath --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed." }

    $Exe = Join-Path $BuildPath "Release\tasket-httpd.exe"
    if (-not (Test-Path -LiteralPath $Exe)) { throw "Built EXE not found: $Exe" }

    Write-Step "Deploy Qt runtime DLLs beside EXE"
    $Deploy = Join-Path $QtPrefix "bin\windeployqt.exe"
    if (-not (Test-Path -LiteralPath $Deploy)) { throw "windeployqt not found: $Deploy" }
    & $Deploy $Exe
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed." }
    Test-RuntimeLayout $Exe

    Write-Host "ok: built EXE: $Exe"
}

if ($HealthCheck) {
    $Exe = Join-Path $BuildPath "Release\tasket-httpd.exe"
    if (-not (Test-Path -LiteralPath $Exe)) { throw "Cannot run health check; EXE not found: $Exe" }

    Write-Step "Health check daemon"
    $proc = Start-Process -FilePath $Exe -ArgumentList @("--port", "7777", "--bind", "127.0.0.1", "--dir", "$env:APPDATA\Tasket++\saved_tasks", "--default-delay", "1") -PassThru -WindowStyle Hidden
    try {
        Start-Sleep -Seconds 2
        $health = Invoke-RestMethod "http://127.0.0.1:7777/health"
        $health | ConvertTo-Json -Depth 6
    } finally {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
}

if ($CleanQtAfterBuild) {
    Write-Step "Nuke disposable Qt cache"
    if (-not (Test-Path -LiteralPath $Sentinel)) {
        throw "Refusing to delete '$QtCacheRoot' because sentinel is missing: $Sentinel"
    }
    Remove-Item -LiteralPath $QtCacheRoot -Recurse -Force
    Write-Host "ok: removed $QtCacheRoot"
}

Write-Step "Done"
Write-Host "Fresh-machine Qt6 build flow complete."
