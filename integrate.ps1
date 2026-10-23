$ErrorActionPreference = 'Stop'
& python (Join-Path $PSScriptRoot 'scripts/integrate.py') @args
exit $LASTEXITCODE
