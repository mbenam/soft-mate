# hw_capture_one.ps1 — capture + evaluate ONE Tier-2 shape probe.
# Assumes the matching probe_shape_<Shape>.m8s is already loaded on the device.
# Captures off hardware, checks health (§6.1) and pitch vs the local render oracle,
# writes a per-probe result JSON, prints one gate line. Exit 0 = pass, 1 = fail.

param (
    [Parameter(Mandatory=$true)][string]$Shape,   # e.g. "00", "A0"
    [string]$Port  = "COM3",
    [string]$Audio = "M8",
    [string]$OutDir = "hwtest_out",
    [int]$Seconds = 3
)

$ErrorActionPreference = "Stop"

$ExeDir = if (Test-Path "build\Release\m8_capture.exe") { "build\Release" }
          elseif (Test-Path "build_asan\Release\m8_capture.exe") { "build_asan\Release" }
          else { Write-Host "ERROR: binaries not found"; exit 2 }

$capDir    = "$OutDir/captures"
$oracleWav = "$OutDir/oracles/shape_$Shape.wav"
if (-not (Test-Path $capDir)) { New-Item -ItemType Directory -Force -Path $capDir | Out-Null }
if (-not (Test-Path $oracleWav)) { Write-Host "ERROR: missing oracle $oracleWav"; exit 2 }

$capWav    = "$capDir/shape_$Shape.wav"
$healthJson= "$capDir/shape_${Shape}_health.json"
$pitchJson = "$capDir/shape_${Shape}_pitch.json"
foreach ($f in @($capWav,$healthJson,$pitchJson)) { if (Test-Path $f) { Remove-Item $f } }

# 1. Capture off hardware (PLAY toggle = 0x08 for both start and stop, pinned).
& "$ExeDir\m8_capture.exe" --port $Port --audio $Audio --seconds $Seconds `
    --start-mask 0x08 --stop-mask 0x08 --out $capWav
if (-not (Test-Path $capWav)) { Write-Host "ERROR: capture produced no wav"; exit 2 }

# 2. Health + 3. Pitch vs oracle
& "$ExeDir\m8_analyze.exe"  $capWav --json $healthJson | Out-Null
& "$ExeDir\m8_spectrum.exe" --ref $capWav --test $oracleWav --json $pitchJson | Out-Null
if (-not (Test-Path $healthJson) -or -not (Test-Path $pitchJson)) {
    Write-Host "ERROR: analyze/spectrum did not emit json"; exit 2
}

$h = Get-Content $healthJson | ConvertFrom-Json
$p = Get-Content $pitchJson  | ConvertFrom-Json

$nonFinite = $h.metrics.non_finite
$peak      = $h.metrics.peak
$silence   = $h.metrics.longest_silence_sec
$duration  = $h.duration_sec
$healthOk  = ($nonFinite -eq 0) -and ($peak -lt 1.0) -and ($peak -gt 0.05) -and `
             ($silence -lt 0.5) -and ($duration -ge 1.5)

$fundRef  = $p.fundamental_ref_hz      # the CAPTURE (ref)
$fundTest = $p.fundamental_test_hz     # the oracle render (test)
$fundOk   = $p.fundamental_ok -eq $true
$withinC4 = ([Math]::Abs($fundRef - 261.63) / 261.63) -lt 0.03
$pitchOk  = $fundOk -and $withinC4

$pass = $healthOk -and $pitchOk
$failure = $null
if (-not $healthOk) { $failure = "capture_health" }
elseif (-not $pitchOk) { $failure = "pitch" }

$result = [ordered]@{
    shape = $Shape
    pass  = [bool]$pass
    capture_health = [ordered]@{
        non_finite = [int]$nonFinite; peak = [double]$peak
        longest_silence_sec = [double]$silence; duration_sec = [double]$duration
        pass = [bool]$healthOk
    }
    pitch = [ordered]@{
        fundamental_capture_hz = [double]$fundRef
        fundamental_oracle_hz  = [double]$fundTest
        fundamental_ok = [bool]$fundOk; within_c4 = [bool]$withinC4; pass = [bool]$pitchOk
    }
    timbre_recorded_not_gated = [ordered]@{
        log_spectral_distance_db = [double]$p.log_spectral_distance_db
        centroid_capture_hz = [double]$p.centroid_ref_hz
        centroid_oracle_hz  = [double]$p.centroid_test_hz
    }
    failure = $failure
}
$result | ConvertTo-Json -Depth 5 | Out-File -FilePath "$capDir/shape_${Shape}_result.json" -Encoding utf8

$tag = if ($pass) { "PASS" } else { "FAIL ($failure)" }
Write-Host ("shape_{0}: {1}  peak={2:N3} sil={3:N3} dur={4:N2}s  capFund={5:N1}Hz oracFund={6:N1}Hz withinC4={7}  LSD={8:N1}dB" -f `
    $Shape, $tag, $peak, $silence, $duration, $fundRef, $fundTest, $withinC4, $p.log_spectral_distance_db)

if ($pass) { exit 0 } else { exit 1 }
