# ui_sweep.ps1 — C7: Drive m8_nav through all navigable screens and write
# one UiCapture JSON per screen into a corpus directory.
#
# Usage:
#   .\tools\ui_sweep.ps1 -Port COM4 -CorpusDir tests/ui/golden/device
#
# Requirements:
#   - m8_nav.exe built in build\Release\
#   - M8 connected and in a known state (Song screen, default theme, font_mode=0)
#
# Output:
#   <CorpusDir>\<SCREEN>.json   — one file per successfully captured screen
#   <CorpusDir>\_sweep_log.txt  — pass/fail summary
#
# Guardrail: UNSETTLED or error results are recorded, not silently skipped.
# Every capture must come from a settled read (--ui-capture uses double-read).

param(
    [Parameter(Mandatory=$true)]
    [string]$Port,

    [string]$CorpusDir = "tests/ui/golden/device",

    [string]$Nav = ".\build\Release\m8_nav.exe",

    [int]$HoldMs = 15
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Screens reachable via --goto-screen. Skips modals (LOAD_PROJECT_MODAL,
# FILE_BROWSER) — they require preconditions that the sweep does not set up.
$screens = @(
    "SONG",
    "CHAIN",
    "PHRASE",
    "INSTRUMENT",
    "TABLE",
    "PROJECT",
    "GROOVE",
    "SCALE",
    "MIXER",
    "EFFECTS",
    "MODS"
)

# Create corpus directory.
if (-not (Test-Path $CorpusDir)) {
    New-Item -ItemType Directory -Path $CorpusDir | Out-Null
    Write-Host "Created corpus directory: $CorpusDir"
}

$logPath = Join-Path $CorpusDir "_sweep_log.txt"
$log     = @()
$passed  = 0
$failed  = @()

Write-Host "ui_sweep: port=$Port  corpus=$CorpusDir  screens=$($screens.Count)"
Write-Host ""

foreach ($screen in $screens) {
    $outFile = Join-Path $CorpusDir "$screen.json"
    Write-Host -NoNewline "  $screen ... "

    # Run m8_nav: navigate to screen, then capture.
    $args = @(
        "--port", $Port,
        "--goto-screen", $screen,
        "--ui-capture", $outFile,
        "--hold-ms", "$HoldMs"
    )

    $proc = Start-Process -FilePath $Nav -ArgumentList $args `
                          -PassThru -Wait -NoNewWindow `
                          -RedirectStandardOutput "$env:TEMP\m8nav_out.txt" `
                          -RedirectStandardError  "$env:TEMP\m8nav_err.txt"

    $stdout = Get-Content "$env:TEMP\m8nav_out.txt" -Raw -ErrorAction SilentlyContinue
    $stderr = Get-Content "$env:TEMP\m8nav_err.txt" -Raw -ErrorAction SilentlyContinue

    if ($proc.ExitCode -eq 0 -and (Test-Path $outFile)) {
        Write-Host "OK"
        $log += "PASS  $screen -> $outFile"
        $passed++
    } else {
        $reason = $stderr.Trim()
        if (-not $reason) { $reason = "exit=$($proc.ExitCode)" }
        Write-Host "FAIL  ($reason)"
        $log += "FAIL  $screen : $reason"
        $failed += $screen
    }
}

Write-Host ""
Write-Host "sweep complete: $passed passed, $($failed.Count) failed"
if ($failed.Count -gt 0) {
    Write-Host "failed screens: $($failed -join ', ')"
}

# Write log.
$log | Out-File -FilePath $logPath -Encoding utf8
Write-Host "log: $logPath"

# Exit non-zero if any screen failed, so CI can catch it.
if ($failed.Count -gt 0) { exit 1 }
