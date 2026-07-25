# ui_sweep.ps1 — C7 / D4 / D5 / D6: Drive m8_nav --serve daemon through all
# navigable screens, per-field cursor states, transport states, and modals,
# writing UiCapture JSON files into a corpus directory.
#
# Usage:
#   .\tools\ui_sweep.ps1 -Port COM4 -CorpusDir tests/ui/golden/device
#
# Requirements:
#   - m8_nav.exe built in build\Release\
#   - M8 connected and in a known state (default theme, font_mode=0)
#
# Output:
#   <CorpusDir>\<SCREEN>.json
#   <CorpusDir>\<SCREEN>__<FIELD>.json
#   <CorpusDir>\<SCREEN>__<FIELD>__<MODIFIER>.json
#   <CorpusDir>\_sweep_log.txt  — pass/fail summary

param(
    [Parameter(Mandatory=$true)]
    [string]$Port,

    [string]$CorpusDir = "tests/ui/golden/device",

    [string]$Nav = ".\build\Release\m8_nav.exe",

    [int]$HoldMs = 15
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Screens reachable via GOTO.
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

# Screens that support transport PLAYING state capture
$transportScreens = @("SONG", "CHAIN", "PHRASE")

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
    $depth = 0
    $started = $false
    while ($true) {
        $line = $proc.StandardOutput.ReadLine()
        if ($null -eq $line) { break }

        $opens = ([regex]::Matches($line, "\{")).Count
        $closes = ([regex]::Matches($line, "\}")).Count

        if (-not $started) {
            if ($opens -gt 0) {
                $started = $true
            } else {
                continue
            }
        }

        $depth += ($opens - $closes)
        $resp += $line + "`n"

        if ($started -and $depth -le 0) {
            break
        }
    }
    return $resp
}

function Do-Capture($tag, $outFileName) {
    $safeOutFile = (Join-Path $CorpusDir $outFileName).Replace("\", "/")
    Write-Host -NoNewline "  $tag ... "
    $proc.StandardInput.WriteLine("CAPTURE path=$safeOutFile")
    $capResp = Read-DaemonResponse

    if (($capResp -match '"code":\s*0' -or $capResp -match '"ok":\s*true') -and (Test-Path $safeOutFile)) {
        Write-Host "OK"
        $script:log += "PASS  $tag -> $safeOutFile"
        $script:passed++
        return $true
    } else {
        Write-Host "FAIL (CAPTURE failed)"
        $script:log += "FAIL  $tag : CAPTURE failed -> $capResp"
        $script:failed += $tag
        return $false
    }
}

# First, exit any file browser modal if device started in one
$proc.StandardInput.WriteLine("PRESS key=OPT")
$null = Read-DaemonResponse

# ---------------------------------------------------------------------------
# 1. Main Navigable Screens & Per-Field Cursor Walk (D5)
# ---------------------------------------------------------------------------

foreach ($screen in $screens) {
    # 1a. GOTO screen
    $proc.StandardInput.WriteLine("GOTO screen=$screen")
    $gotoResp = Read-DaemonResponse

    if ($gotoResp -notmatch '"code":\s*0' -and $gotoResp -notmatch '"ok":\s*true') {
        Write-Host "  $screen ... FAIL (GOTO failed)"
        $log += "FAIL  $screen : GOTO failed -> $gotoResp"
        $failed += $screen
        continue
    }

    # Capture base landing screen
    $null = Do-Capture $screen "$screen.json"

    # Get field map for this screen
    $proc.StandardInput.WriteLine("FIELDS screen=$screen")
    $fieldsResp = Read-DaemonResponse
    $fields = @()
    try {
        $jsonObj = $fieldsResp | ConvertFrom-Json
        if ($jsonObj.fields) { $fields = @($jsonObj.fields) }
    } catch {}

    # For grid screens, sample 4 representative steps (STEP0, STEP4, STEP8, STEPC)
    # For form screens, limit to max 16 fields to keep corpus bounded
    if ($fields.Count -eq 16 -and $fields[0] -eq "STEP0") {
        $fields = @("STEP0", "STEP4", "STEP8", "STEPC")
    } elseif ($fields.Count -gt 16) {
        $fields = $fields[0..15]
    }

    foreach ($field in $fields) {
        if (-not $field) { continue }
        # Move cursor to field
        $proc.StandardInput.WriteLine("CURSOR field=$field")
        $curResp = Read-DaemonResponse
        if ($curResp -match '"code":\s*0' -or $curResp -match '"ok":\s*true') {
            $tag = "${screen}__${field}"
            $null = Do-Capture $tag "${tag}.json"
        } else {
            $tag = "${screen}__${field}"
            Write-Host "  $tag ... FAIL (CURSOR failed)"
            $log += "FAIL  $tag : CURSOR failed -> $curResp"
            $failed += $tag
        }
    }

    # Transport PLAYING state (D6) for SONG, CHAIN, PHRASE
    if ($transportScreens -contains $screen) {
        # Start playback
        $proc.StandardInput.WriteLine("PRESS key=SHIFT|PLAY")
        $null = Read-DaemonResponse
        Start-Sleep -Milliseconds 200

        $tag = "${screen}__PLAYING"
        $null = Do-Capture $tag "${tag}.json"

        # Stop playback (restore state)
        $proc.StandardInput.WriteLine("PRESS key=SHIFT|PLAY")
        $null = Read-DaemonResponse
        Start-Sleep -Milliseconds 200
    }
}

# ---------------------------------------------------------------------------
# 2. Modal Screens (D6): LOAD_PROJECT_MODAL
# ---------------------------------------------------------------------------

# Open LOAD_PROJECT_MODAL browser via GOTO LOADPROJECT (triggers openLoadModal)
$proc.StandardInput.WriteLine("GOTO screen=LOADPROJECT")
$modalResp = Read-DaemonResponse

if ($modalResp -match '"code":\s*0' -or $modalResp -match '"ok":\s*true') {
    $null = Do-Capture "LOAD_PROJECT_MODAL" "LOAD_PROJECT_MODAL.json"

    # Restore state: exit modal with OPT
    $proc.StandardInput.WriteLine("PRESS key=OPT")
    $null = Read-DaemonResponse
} else {
    Write-Host "  LOAD_PROJECT_MODAL ... FAIL (LOADPROJECT modal open failed)"
    $log += "FAIL  LOAD_PROJECT_MODAL : open failed -> $modalResp"
    $failed += "LOAD_PROJECT_MODAL"
}

# Close daemon session.
$proc.StandardInput.WriteLine("QUIT")
$null = Read-DaemonResponse
$proc.WaitForExit(3000)

Write-Host ""
Write-Host "sweep complete: $passed passed, $($failed.Count) failed"
if ($failed.Count -gt 0) {
    Write-Host "failed states: $($failed -join ', ')"
}

# Write log.
$log | Out-File -FilePath $logPath -Encoding utf8
Write-Host "log: $logPath"

# Exit non-zero if any state failed.
if ($failed.Count -gt 0) { exit 1 }
