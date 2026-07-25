# ui_sweep.ps1 — C7/D4: Drive m8_nav --serve daemon through all navigable
# screens and write one UiCapture JSON per screen into a corpus directory.
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
# Single-process daemon: opens serial port once (--serve) and sends GOTO / CAPTURE
# commands over stdin. `'R'` frame reset sent only once per run.

param(
    [Parameter(Mandatory=$true)]
    [string]$Port,

    [string]$CorpusDir = "tests/ui/golden/device",

    [string]$Nav = ".\build\Release\m8_nav.exe",

    [int]$HoldMs = 15
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Screens reachable via GOTO. Skips modals (LOAD_PROJECT_MODAL, FILE_BROWSER).
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

Write-Host "ui_sweep (daemon): port=$Port  corpus=$CorpusDir  screens=$($screens.Count)"
Write-Host ""

# Start single m8_nav --serve daemon process.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = (Resolve-Path $Nav).Path
$psi.Arguments = "--port $Port --serve --hold-ms $HoldMs"
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true

$proc = [System.Diagnostics.Process]::Start($psi)

function Read-DaemonResponse {
    $resp = ""
    $inJson = $false
    while ($true) {
        $line = $proc.StandardOutput.ReadLine()
        if ($null -eq $line) { break }
        if (-not $inJson) {
            if ($line.Trim() -eq "{") {
                $inJson = $true
                $resp += "{" + "`n"
            }
            continue
        }
        $resp += $line + "`n"
        if ($line.Trim() -eq "}") { break }
    }
    return $resp
}

# First, exit file browser modal if device started in one
$proc.StandardInput.WriteLine("PRESS key=OPT")
$null = Read-DaemonResponse

foreach ($screen in $screens) {
    # Replace any characters not allowed in file paths
    $safeOutFile = (Join-Path $CorpusDir "$screen.json").Replace("\", "/")
    Write-Host -NoNewline "  $screen ... "

    # 1. GOTO screen
    $proc.StandardInput.WriteLine("GOTO screen=$screen")
    $gotoResp = Read-DaemonResponse

    if ($gotoResp -notmatch '"code":\s*0' -and $gotoResp -notmatch '"ok":\s*true') {
        Write-Host "FAIL (GOTO failed)"
        $log += "FAIL  $screen : GOTO failed -> $gotoResp"
        $failed += $screen
        continue
    }

    # 2. CAPTURE screen
    $proc.StandardInput.WriteLine("CAPTURE path=$safeOutFile")
    $capResp = Read-DaemonResponse

    if (($capResp -match '"code":\s*0' -or $capResp -match '"ok":\s*true') -and (Test-Path $safeOutFile)) {
        Write-Host "OK"
        $log += "PASS  $screen -> $safeOutFile"
        $passed++
    } else {
        Write-Host "FAIL (CAPTURE failed)"
        $log += "FAIL  $screen : CAPTURE failed -> $capResp"
        $failed += $screen
    }
}

# Close daemon session.
$proc.StandardInput.WriteLine("QUIT")
$null = Read-DaemonResponse
$proc.WaitForExit(3000)

Write-Host ""
Write-Host "sweep complete: $passed passed, $($failed.Count) failed"
if ($failed.Count -gt 0) {
    Write-Host "failed screens: $($failed -join ', ')"
}

# Write log.
$log | Out-File -FilePath $logPath -Encoding utf8
Write-Host "log: $logPath"

# Exit non-zero if any screen failed.
if ($failed.Count -gt 0) { exit 1 }
