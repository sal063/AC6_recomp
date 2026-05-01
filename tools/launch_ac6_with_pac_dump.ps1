[CmdletBinding()]
param(
    # Enable the PAC stream-worker dispatch probe. When set, every distinct
    # work-item virtual that rex_sub_82343E18 dispatches gets logged once
    # via `[AC6 PAC WORKER] new dispatch target=...`. Cross-reference these
    # against compressed-entry writes to identify the mode-1 decoder.
    [switch]$TraceWorkItems,

    # Enable per-NtReadFile guest stack traces on PAC reads. Each call into
    # NtReadFile / NtReadFileScatter for a PAC path logs `stack=[...]` with
    # the full guest back-chain. Used to pin the decoder when it sits above
    # the read-issuing function on the reader thread's call chain.
    [switch]$TraceStacks
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $repoRoot 'out\build\win-amd64-relwithdebinfo\ac6recomp.exe'

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "ac6recomp.exe not found at $exePath"
}

$env:AC6_DUMP_PAC_DECODED = '1'
Write-Host "AC6_DUMP_PAC_DECODED=1"

if ($TraceWorkItems) {
    $env:AC6_TRACE_PAC_WORK_ITEMS = '1'
    Write-Host "AC6_TRACE_PAC_WORK_ITEMS=1"
} else {
    Remove-Item Env:AC6_TRACE_PAC_WORK_ITEMS -ErrorAction SilentlyContinue
}

if ($TraceStacks) {
    $env:AC6_TRACE_PAC_STACKS = '1'
    Write-Host "AC6_TRACE_PAC_STACKS=1"
} else {
    Remove-Item Env:AC6_TRACE_PAC_STACKS -ErrorAction SilentlyContinue
}

Write-Host "Launching $exePath"

Start-Process -FilePath $exePath -WorkingDirectory (Split-Path -Parent $exePath)
