$fields = @("TYPE", "NAME", "TRANSP", "SAMPLE", "SLICE", "AMP", "PLAY", "LIM", "START", "PAN", "LOOP_ST", "DRY", "LENGTH", "CHO", "DETUNE", "DEL", "DEGRADE", "REV", "FILTER", "CUTOFF", "RES", "TEMPO", "TRANSPOSE", "GROOVE", "SCALE", "QUANTIZE", "MIDI", "PROJECT", "EXPORT", "CLEAR", "INST.POOL", "TIME", "SYSTEM", "SAMPLEROOT", "CHO_EQ", "CHO_MOD_DEP", "CHO_WID", "CHO_REV", "DEL_EQ", "DEL_TIME_L", "DEL_FBK", "DEL_WID", "DEL_REV", "REV_EQ", "REV_SIZE", "REV_DEC", "REV_MOD_DEP", "REV_WID", "OUT_VOL", "MST_CHO", "MST_DEL", "MST_REV", "IN_VOL", "USB_VOL", "MIX_VOL", "LIM_VAL", "DJF_FREQ", "DJF_RES", "DJF_TYP", "TUNE", "LOAD", "SAVE")

$port = "com3"
$passes = 0
$total = 0

Write-Host "=== STARTING FIELD LANDING RELIABILITY TEST ==="
Write-Host "Testing $($fields.Count) fields, 3 runs each ($($fields.Count * 3) total runs)..."
Write-Host ""

foreach ($field in $fields) {
    for ($run = 1; $run -le 3; $run++) {
        $total++
        Write-Host "Run ${run}: ${field}... " -NoNewline
        
        $output = & .\build\Release\m8_nav.exe --port $port --read-field $field --hold-ms 15 2>&1
        $exitCode = $LASTEXITCODE
        
        if ($exitCode -eq 0 -and $output -match "$field\s*=\s*\S+") {
            $passes++
            Write-Host "Success ($output)" -ForegroundColor Green
        } else {
            Write-Host "FAILED!" -ForegroundColor Red
            Write-Host "  Exit Code: $exitCode"
            Write-Host "  Output: $output"
            Write-Host ""
        }
    }
}

Write-Host ""
Write-Host "=== TEST COMPLETE ==="
if ($passes -eq $total) {
    Write-Host "Result: $passes / $total passes" -ForegroundColor Green
    exit 0
} else {
    Write-Host "Result: $passes / $total passes" -ForegroundColor Red
    exit 1
}
