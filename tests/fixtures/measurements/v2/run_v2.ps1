# Automated V2 amplitude parity measurement runner
# Compares hardware probe captures vs golden captures at volume=0x40 (headroom)

$Types = @("Sampler", "MacroSynth", "WavSynth", "FMSynth", "HyperSynth")
foreach ($t in $Types) {
    Write-Host "Running spectrum comparison for $t..."
    .\build\Release\m8_spectrum.exe --ref "tests/fixtures/device_golden/${t}_headroom.wav" --test "probes/${t}_headroom.wav" --record "tests/fixtures/measurements/v2/${t}.record.json"
}
