[CmdletBinding()]
param(
    [string]$QtVersion = "",
    [string]$QtArch = "win64_msvc2022_64",
    [switch]$DryRun,
    [switch]$KeepQt,
    [switch]$RequireNewestQt,
    [switch]$FreshTasketClone
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$PinFile = Join-Path $Root "QT_VERSION.txt"
$BuildDir = Join-Path $Root "build-msvc"
$QtCache = Join-Path $Root "_qt6-build-cache"
$QtSentinel = Join-Path $QtCache ".macrohelp-owned-qt-cache"
$LocalTasket = Join-Path $Root "original"

if (-not $QtVersion) {
    if (-not (Test-Path -LiteralPath $PinFile)) {
        $QtVersion = "latest-compatible"
    } else {
        $QtVersion = (Get-Content -LiteralPath $PinFile -TotalCount 1).Trim()
        if (-not $QtVersion) {
            $QtVersion = "latest-compatible"
        }
        Write-Host "Using pinned Qt version from QT_VERSION.txt: $QtVersion"
    }
}

if ($DryRun) {
    & (Join-Path $Root "scripts\build-fresh-qt6.ps1") -QtVersion $QtVersion -QtArch $QtArch -DryRun -RequireNewestQt:$RequireNewestQt
    exit $LASTEXITCODE
}

if (Test-Path -LiteralPath $BuildDir) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

if (Test-Path -LiteralPath $QtCache) {
    if (-not (Test-Path -LiteralPath $QtSentinel)) {
        throw "Refusing to remove Qt cache because sentinel is missing: $QtSentinel"
    }
    Remove-Item -LiteralPath $QtCache -Recurse -Force
}

if ($FreshTasketClone -and (Test-Path -LiteralPath $LocalTasket)) {
    Remove-Item -LiteralPath $LocalTasket -Recurse -Force
}

$childParams = @{
    QtVersion = $QtVersion
    QtArch = $QtArch
    InstallQt = $true
    Build = $true
    HealthCheck = $true
    CleanQtAfterBuild = (-not $KeepQt)
    RequireNewestQt = [bool]$RequireNewestQt
}

& (Join-Path $Root "scripts\build-fresh-qt6.ps1") @childParams
