[CmdletBinding()]
param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\build-msvc\Release\tasket-httpd.exe')
)

$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $ExePath).Path
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('macrohelp-tasket23-' + [guid]::NewGuid())
$tasksDir = Join-Path $testRoot 'tasks with spaces'
New-Item -ItemType Directory -Path $tasksDir -Force | Out-Null
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()
$base = "http://127.0.0.1:$port"
$daemon = $null

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

try {
    $daemon = Start-Process -FilePath $exe -ArgumentList "--port $port --bind 127.0.0.1 --dir `"$tasksDir`" --default-delay 0" -PassThru -WindowStyle Hidden
    $healthy = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if ($daemon.HasExited) { throw "Test daemon exited with code $($daemon.ExitCode)." }
        try { $healthy = (Invoke-RestMethod "$base/health" -TimeoutSec 1).data.status -eq 'ok' } catch { }
        if ($healthy) { break }
        Start-Sleep -Milliseconds 100
    }
    Assert-True $healthy 'Daemon did not become healthy.'
    Assert-True ((Invoke-RestMethod "$base/").data.version -eq '2.3.0') 'Unexpected daemon version.'

    # Check the new options without ever executing a power/session command.
    foreach ($command in @('shutdown', 'restart', 'logoff')) {
        foreach ($force in @('Force', "Don't force")) {
            $body = @{
                name = 'force-option'; run = $false
                task = @{ docType = 'ScheduleTask File'; actions = @(@{
                    type = 'systemcommand'; sysCommandType = $command
                    sysCommandParam1 = $force; sysCommandParam2 = ''
                }) }
            } | ConvertTo-Json -Depth 8
            $written = Invoke-RestMethod "$base/temp-task" -Method Post -ContentType 'application/json' -Body $body
            $saved = Get-Content -LiteralPath $written.data.path -Raw | ConvertFrom-Json
            Assert-True ($saved.actions[0].sysCommandParam1 -eq $force) 'Force option was not preserved.'
            Assert-True (-not $written.data.scheduled) 'Write-only task was unexpectedly scheduled.'
        }
    }

    # Exercise Qt6 program/argument separation with a spaced executable and output path.
    $shell = (Get-Command pwsh -ErrorAction Stop).Source
    $marker = Join-Path $testRoot 'result with spaces.txt'
    $escapedMarker = $marker.Replace("'", "''")
    $body = @{
        name = 'argument-check'; run = $true; cleanup = $true; delay = 0
        task = @{ docType = 'ScheduleTask File'; actions = @(
            @{ type = 'systemcommand'; sysCommandType = 'executeprogram'; sysCommandParam1 = $shell
               sysCommandParam2 = "-NoLogo -NoProfile -NonInteractive -Command `"[IO.File]::WriteAllText('$escapedMarker', 'arguments with spaces')`"" },
            @{ type = 'wait'; duration = 0.01 }
        ) }
    } | ConvertTo-Json -Depth 8
    $scheduled = Invoke-RestMethod "$base/temp-task" -Method Post -ContentType 'application/json' -Body $body
    $taskId = $scheduled.data.task.task_number
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        $state = (Invoke-RestMethod "$base/check?id=$taskId").data.state
        if ($state -eq 'failed') { throw 'Program-launch task failed.' }
        if ($state -eq 'finished' -and (Test-Path -LiteralPath $marker)) { break }
        Start-Sleep -Milliseconds 100
    }
    Assert-True ($state -eq 'finished') 'Program-launch task did not finish.'
    Assert-True ((Get-Content -LiteralPath $marker -Raw) -eq 'arguments with spaces') 'Program arguments were not delivered correctly.'
    Write-Host 'ok: Tasket 2.3 health, six force-option round trips, and Qt6 program arguments'
} finally {
    if ($daemon -and -not $daemon.HasExited) { Stop-Process -Id $daemon.Id -Force }
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    $cleanupTarget = [IO.Path]::GetFullPath($testRoot)
    if (-not $cleanupTarget.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $cleanupTarget -Leaf) -notlike 'macrohelp-tasket23-*') {
        throw "Refusing to clean unexpected test directory: $cleanupTarget"
    }
    Remove-Item -LiteralPath $cleanupTarget -Recurse -Force
}
