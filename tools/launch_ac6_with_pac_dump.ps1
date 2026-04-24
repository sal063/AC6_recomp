$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $repoRoot 'out\build\win-amd64-relwithdebinfo\ac6recomp.exe'

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "ac6recomp.exe not found at $exePath"
}

$env:AC6_DUMP_PAC_DECODED = '1'
Write-Host "AC6_DUMP_PAC_DECODED=1"
Write-Host "Launching $exePath"

Start-Process -FilePath $exePath -WorkingDirectory (Split-Path -Parent $exePath)
