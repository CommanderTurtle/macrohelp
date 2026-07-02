[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BinDir
)

$ErrorActionPreference = "Stop"
$resolved = (Resolve-Path $BinDir).Path

$required = @(
    "tasket-httpd.exe",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Network.dll",
    "Qt6Widgets.dll",
    "platforms\qwindows.dll"
)

$missing = @()
foreach ($item in $required) {
    $path = Join-Path $resolved $item
    if (-not (Test-Path -LiteralPath $path)) {
        $missing += $item
    }
}

if ($missing.Count -gt 0) {
    Write-Error "Runtime folder is not standalone. Missing: $($missing -join ', ')"
    exit 1
}

Write-Host "ok: standalone Qt runtime layout verified at $resolved"
