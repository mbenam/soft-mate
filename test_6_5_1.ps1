$screens = @("SONG", "CHAIN", "PHRASE", "INSTRUMENT", "TABLE", "GROOVE", "MODS", "SCALE", "INST.POOL", "MIXER", "EFFECTS", "PROJECT")

$success = 0
$total = 0

foreach ($s in $screens) {
    for ($i = 1; $i -le 3; $i++) {
        Write-Host "Run $i from $s to PROJECT"
        
        # First go to the starting screen
        $p = Start-Process -NoNewWindow -Wait -PassThru C:\dev\m8-sdl3\build\Release\m8_nav.exe "--port com3 --goto-screen $s"
        if ($p.ExitCode -ne 0) {
            Write-Host "  Failed to reach starting screen $s"
            continue
        }

        # Then go to PROJECT
        $p = Start-Process -NoNewWindow -Wait -PassThru C:\dev\m8-sdl3\build\Release\m8_nav.exe "--port com3 --goto-screen PROJECT"
        $total++
        if ($p.ExitCode -eq 0) {
            $success++
            Write-Host "  Success"
        } else {
            Write-Host "  FAILED!"
        }
    }
}

Write-Host "Result: $success / $total passes"
