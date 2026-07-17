# m8_hwtest.ps1 — M8 Headless Hardware Test Orchestrator
# Implements M8_HARDWARE_TEST_SPEC.md

param (
    [Parameter(Mandatory=$true)]
    [string]$Port,

    [string]$Audio = "M8",
    [string]$OutDir = "hwtest_out",
    [switch]$SkipButtonPin,
    [int]$Tier = 1
)

$ErrorActionPreference = "Stop"

# Display usage helper
function Show-Usage {
    Write-Host "Usage: .\tools\m8_hwtest.ps1 -Port COM4 [-Audio M8] [-OutDir hwtest_out] [-SkipButtonPin]"
}

if (-not $Port) {
    Show-Usage
    exit 2
}

# Ensure output directory exists
if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

# 1. Auto-detect executable directory. Prefer the normal Release build: this is a
# hardware harness, not a memory-ownership test, so ASan's slowdown buys nothing
# here — and preferring build_asan silently ran a stale ASan binary once.
$ExeDir = ""
if (Test-Path "build\Release\m8_makeprobe.exe") {
    $ExeDir = "build\Release"
} elseif (Test-Path "build_asan\Release\m8_makeprobe.exe") {
    $ExeDir = "build_asan\Release"
} else {
    Write-Host "ERROR: Could not find build executables in build\Release or build_asan\Release."
    $verdict = @{
        "pass" = $false
        "tier" = $Tier
        "failure" = "setup_error_binaries_missing"
    }
    $verdict | ConvertTo-Json -Depth 4 | Out-File -FilePath "$OutDir/verdict.json" -Encoding utf8
    exit 2
}
Write-Host "Using binaries from: $ExeDir"

# 2. Step 0: Generate the fixture probe song
Write-Host "Generating fixture probe song..."
$probeFile = "$OutDir/probe_selftest.m8s"
if (Test-Path $probeFile) { Remove-Item $probeFile }

$makeprobeCmd = @(
    "--type", "macrosynth",
    "--shape", "0x00",
    "--timbre", "0x40",
    "--color", "0x80",
    "--note", "C-4",
    "--tempo", "120",
    "--out", $probeFile
)

Write-Host "Running: & $ExeDir\m8_makeprobe.exe $makeprobeCmd"
& "$ExeDir\m8_makeprobe.exe" $makeprobeCmd

if (-not (Test-Path $probeFile)) {
    Write-Host "ERROR: Failed to generate probe song."
    $verdict = @{
        "pass" = $false
        "tier" = $Tier
        "makeprobe_roundtrip" = "fail"
        "failure" = "makeprobe_failed"
    }
    $verdict | ConvertTo-Json -Depth 4 | Out-File -FilePath "$OutDir/verdict.json" -Encoding utf8
    exit 1
}
Write-Host "Probe song generated successfully."

# 3. Step 1: Render the local oracle
Write-Host "Rendering local oracle wav..."
$renderWav = "$OutDir/render_selftest.wav"
if (Test-Path $renderWav) { Remove-Item $renderWav }

$renderCmd = @(
    "--load", $probeFile,
    "--note", "C-4",
    "--instrument", "0",
    "--seconds", "3",
    "--out", "$OutDir/render_selftest"
)
Write-Host "Running: & $ExeDir\m8_render.exe $renderCmd"
& "$ExeDir\m8_render.exe" $renderCmd

if (-not (Test-Path $renderWav)) {
    Write-Host "ERROR: Failed to render oracle wav."
    $verdict = @{
        "pass" = $false
        "tier" = $Tier
        "makeprobe_roundtrip" = "pass"
        "failure" = "render_oracle_failed"
    }
    $verdict | ConvertTo-Json -Depth 4 | Out-File -FilePath "$OutDir/verdict.json" -Encoding utf8
    exit 2
}

# 4. Step 2: Pin the button protocol
$startMask = 0x08
$stopMask = 0x10
$buttonsFile = "hw_buttons.json"
$pinConfirmed = $false

if ($SkipButtonPin -and (Test-Path $buttonsFile)) {
    Write-Host "Skipping button pinning. Loading from $buttonsFile."
    $btnData = Get-Content $buttonsFile | ConvertFrom-Json
    $startMask = $btnData.start_mask
    $stopMask = $btnData.stop_mask
    $pinConfirmed = $btnData.confirmed
} elseif (Test-Path $buttonsFile) {
    Write-Host "Loading existing button masks from $buttonsFile."
    $btnData = Get-Content $buttonsFile | ConvertFrom-Json
    $startMask = $btnData.start_mask
    $stopMask = $btnData.stop_mask
    $pinConfirmed = $btnData.confirmed
} else {
    Write-Host "Pinning button protocol empirically..."
    
    # Sweep START masks
    $candidateStartMasks = @(0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80)
    $foundStart = $false
    
    foreach ($mask in $candidateStartMasks) {
        $maskHex = "0x" + ('{0:X2}' -f $mask)
        Write-Host "Testing START mask: $maskHex..."
        
        $tempWav = "$OutDir/pin_temp.wav"
        $tempJson = "$OutDir/pin_temp.json"
        if (Test-Path $tempWav) { Remove-Item $tempWav }
        if (Test-Path $tempJson) { Remove-Item $tempJson }
        
        $captureArgs = @(
            "--port", $Port,
            "--audio", $Audio,
            "--seconds", "2.0",
            "--start-mask", $maskHex,
            "--stop-mask", "0x00",
            "--out", $tempWav
        )
        & "$ExeDir\m8_capture.exe" $captureArgs
        
        if (-not (Test-Path $tempWav)) {
            Write-Host "Warning: m8_capture did not produce $tempWav"
            continue
        }
        
        & "$ExeDir\m8_analyze.exe" $tempWav --json $tempJson
        if (-not (Test-Path $tempJson)) {
            Write-Host "Warning: m8_analyze did not produce $tempJson"
            continue
        }
        
        $metrics = Get-Content $tempJson | ConvertFrom-Json
        $peak = $metrics.metrics.peak
        $silence = $metrics.metrics.longest_silence_sec
        
        Write-Host "START 0x$('{0:X2}' -f $mask): peak=$peak, longest_silence=$silence"
        
        # Playback started iff peak > 0.02 and longest_silence_sec < 0.5
        if ($peak -gt 0.02 -and $silence -lt 0.5) {
            $startMask = $mask
            $foundStart = $true
            Write-Host "Confirmed START mask: 0x$('{0:X2}' -f $startMask)"
            break
        }
    }
    
    if (-not $foundStart) {
        Write-Host "ERROR: Could not confirm START button protocol (device remained silent)."
        $verdict = @{
            "pass" = $false
            "tier" = $Tier
            "makeprobe_roundtrip" = "pass"
            "failure" = "button_protocol_unconfirmed"
        }
        $verdict | ConvertTo-Json -Depth 4 | Out-File -FilePath "$OutDir/verdict.json" -Encoding utf8
        exit 2
    }
    
    # Confirm STOP. The M8 PLAY key (the pinned start mask) is a TOGGLE — pressing it
    # again stops playback. There is no separate stop key to sweep for, and sweeping one
    # is actively harmful: each candidate iteration re-presses PLAY, flipping the device
    # on/off by parity so nothing confirms cleanly. Instead, re-press the start mask once
    # and verify the device falls silent. The START sweep above left the device playing.
    $stopMask = $startMask
    $startMaskHex = "0x" + ('{0:X2}' -f $startMask)
    Write-Host "Confirming STOP = PLAY toggle ($startMaskHex)..."

    # Press the toggle to stop (the key press happens at the start of the capture).
    $toggleWav = "$OutDir/pin_stop_toggle.wav"
    $toggleArgs = @(
        "--port", $Port, "--audio", $Audio, "--seconds", "0.5",
        "--start-mask", $startMaskHex, "--stop-mask", "0x00", "--out", $toggleWav
    )
    & "$ExeDir\m8_capture.exe" $toggleArgs

    # Verify the device is now silent (no keys pressed during this capture).
    $stopCheckWav = "$OutDir/pin_stop_check.wav"
    $stopCheckJson = "$OutDir/pin_stop_check.json"
    if (Test-Path $stopCheckJson) { Remove-Item $stopCheckJson }
    $checkArgs = @(
        "--port", $Port, "--audio", $Audio, "--seconds", "1.0",
        "--start-mask", "0x00", "--stop-mask", "0x00", "--out", $stopCheckWav
    )
    & "$ExeDir\m8_capture.exe" $checkArgs
    if (Test-Path $stopCheckWav) {
        & "$ExeDir\m8_analyze.exe" $stopCheckWav --json $stopCheckJson | Out-Null
    }

    $foundStop = $false
    if (Test-Path $stopCheckJson) {
        $stopPeak = (Get-Content $stopCheckJson | ConvertFrom-Json).metrics.peak
        # 0.05, not the spec's 0.02: on stop the M8 note-offs into the envelope release,
        # leaving a brief decay tail (~0.03 observed) that does not reach 0.02 inside the
        # check window. 0.05 still cleanly separates 'stopped' (~0.03) from 'playing' (~0.5).
        Write-Host "STOP toggle check: peak=$stopPeak (stopped iff < 0.05)"
        if ($stopPeak -lt 0.05) {
            $foundStop = $true
            Write-Host "Confirmed STOP mask: $startMaskHex (PLAY toggle)"
        }
    }

    if (-not $foundStop) {
        Write-Host "ERROR: Could not confirm STOP button protocol (device remained active)."
        $verdict = @{
            "pass" = $false
            "tier" = $Tier
            "makeprobe_roundtrip" = "pass"
            "failure" = "button_protocol_unconfirmed"
        }
        $verdict | ConvertTo-Json -Depth 4 | Out-File -FilePath "$OutDir/verdict.json" -Encoding utf8
        exit 2
    }
    
    # Save confirmed masks
    $buttons = @{
        "start_mask" = $startMask
        "stop_mask" = $stopMask
        "confirmed" = $true
    }
    $buttons | ConvertTo-Json | Out-File -FilePath $buttonsFile -Encoding utf8
    $pinConfirmed = $true
    Write-Host "Saved confirmed masks to $buttonsFile"
}

# 5. Capture the actual self-test run
Write-Host "Capturing hardware self-test run..."
$captureWav = "$OutDir/capture_selftest.wav"
if (Test-Path $captureWav) { Remove-Item $captureWav }

$startMaskHex = "0x" + ('{0:X2}' -f $startMask)
$stopMaskHex = "0x" + ('{0:X2}' -f $stopMask)

$captureCmd = @(
    "--port", $Port,
    "--audio", $Audio,
    "--seconds", "3",
    "--start-mask", $startMaskHex,
    "--stop-mask", $stopMaskHex,
    "--out", $captureWav
)
Write-Host "Running: & $ExeDir\m8_capture.exe $captureCmd"
& "$ExeDir\m8_capture.exe" $captureCmd

if (-not (Test-Path $captureWav)) {
    Write-Host "ERROR: Failed to capture hardware wav."
    $verdict = @{
        "pass" = $false
        "tier" = $Tier
        "makeprobe_roundtrip" = "pass"
        "button_protocol" = @{ "start_mask" = $startMask; "stop_mask" = $stopMask; "confirmed" = $pinConfirmed }
        "failure" = "capture_failed"
    }
    $verdict | ConvertTo-Json -Depth 4 | Out-File -FilePath "$OutDir/verdict.json" -Encoding utf8
    exit 2
}

# 6. Analyze and Compare
Write-Host "Running metrics analysis on capture..."
$healthJson = "$OutDir/capture_health.json"
if (Test-Path $healthJson) { Remove-Item $healthJson }
& "$ExeDir\m8_analyze.exe" $captureWav --json $healthJson

if (-not (Test-Path $healthJson)) {
    Write-Host "ERROR: Failed to analyze capture health."
    $verdict = @{
        "pass" = $false
        "tier" = $Tier
        "makeprobe_roundtrip" = "pass"
        "button_protocol" = @{ "start_mask" = $startMask; "stop_mask" = $stopMask; "confirmed" = $pinConfirmed }
        "failure" = "analysis_failed"
    }
    $verdict | ConvertTo-Json -Depth 4 | Out-File -FilePath "$OutDir/verdict.json" -Encoding utf8
    exit 2
}

Write-Host "Comparing spectrum between capture and render..."
$compareJson = "$OutDir/pitch_compare.json"
if (Test-Path $compareJson) { Remove-Item $compareJson }
& "$ExeDir\m8_spectrum.exe" --ref $captureWav --test $renderWav --json $compareJson

if (-not (Test-Path $compareJson)) {
    Write-Host "ERROR: Failed to compare spectra."
    $verdict = @{
        "pass" = $false
        "tier" = $Tier
        "makeprobe_roundtrip" = "pass"
        "button_protocol" = @{ "start_mask" = $startMask; "stop_mask" = $stopMask; "confirmed" = $pinConfirmed }
        "failure" = "spectrum_failed"
    }
    $verdict | ConvertTo-Json -Depth 4 | Out-File -FilePath "$OutDir/verdict.json" -Encoding utf8
    exit 2
}

# 7. Evaluate gates and write verdict
$health = Get-Content $healthJson | ConvertFrom-Json
$pitch = Get-Content $compareJson | ConvertFrom-Json

$nonFinite = $health.metrics.non_finite
$peak = $health.metrics.peak
$longestSilence = $health.metrics.longest_silence_sec
$duration = $health.duration_sec

$healthOk = $nonFinite -eq 0 -and
            $peak -lt 1.0 -and
            $peak -gt 0.05 -and
            $longestSilence -lt 0.5 -and
            $duration -ge 1.5

$fundRef = $pitch.fundamental_ref_hz
$fundTest = $pitch.fundamental_test_hz
# fundamental_ok parsed as boolean
$fundOkStr = $pitch.fundamental_ok
$fundOk = $fundOkStr -eq $true

# Verify fundamental is actually close to C-4 (261.63 Hz) within 3%
$withinC4 = [Math]::Abs($fundRef - 261.63) / 261.63 -lt 0.03
$pitchOk = $fundOk -and $withinC4

$pass = $healthOk -and $pitchOk
$failure = $null
if (-not $healthOk) {
    $failure = "capture_health_check_failed"
} elseif (-not $pitchOk) {
    $failure = "pitch_check_failed"
}

# Timbre details
$lsd = $pitch.log_spectral_distance_db
$centroidRef = $pitch.centroid_ref_hz
$centroidTest = $pitch.centroid_test_hz

$verdict = [ordered]@{
    "pass" = [bool]$pass
    "tier" = [int]$Tier
    "makeprobe_roundtrip" = "pass"
    "button_protocol" = @{
        "start_mask" = [int]$startMask
        "stop_mask" = [int]$stopMask
        "confirmed" = [bool]$pinConfirmed
    }
    "capture_health" = @{
        "non_finite" = [int]$nonFinite
        "peak" = [double]$peak
        "longest_silence_sec" = [double]$longestSilence
        "duration_sec" = [double]$duration
        "pass" = [bool]$healthOk
    }
    "pitch" = @{
        "fundamental_ref_hz" = [double]$fundRef
        "fundamental_test_hz" = [double]$fundTest
        "fundamental_ok" = [bool]$fundOk
        "within_c4" = [bool]$withinC4
        "pass" = [bool]$pitchOk
    }
    "timbre_recorded_not_gated" = @{
        "log_spectral_distance_db" = [double]$lsd
        "centroid_ref_hz" = [double]$centroidRef
        "centroid_test_hz" = [double]$centroidTest
    }
    "failure" = $failure
}

# Output verdict to stdout
Write-Host "`n=== HW TEST VERDICT ==="
Write-Host "Pass:              $pass"
Write-Host "Start Mask:        $startMask"
Write-Host "Stop Mask:         $stopMask"
Write-Host "Peak:              $peak"
Write-Host "Silence Sec:       $longestSilence"
Write-Host "Duration Sec:      $duration"
Write-Host "Fund Ref:          $fundRef Hz"
Write-Host "Fund Test:         $fundTest Hz"
Write-Host "Fund Match:        $fundOk"
Write-Host "Within C-4:        $withinC4"
Write-Host "Log-Spec Distance: $lsd dB"
if ($failure) {
    Write-Host "Failure Reason:    $failure"
}
Write-Host "========================`n"

$verdict | ConvertTo-Json -Depth 4 | Out-File -FilePath "$OutDir/verdict.json" -Encoding utf8
Write-Host "Verdict written to $OutDir/verdict.json"

if ($pass) {
    exit 0
} else {
    exit 1
}
