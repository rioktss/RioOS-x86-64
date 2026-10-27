$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
& (Join-Path $ProjectRoot "tools\x86_64\build.ps1") @args
