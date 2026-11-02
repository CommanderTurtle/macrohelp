$ErrorActionPreference = 'Stop'
& python (Join-Path $PSScriptRoot 'integrate.py') @args
exit $LASTEXITCODE
