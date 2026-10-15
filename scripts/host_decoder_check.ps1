# Local Windows decoder checks; does not discover or access Android devices.
param([string]$VisualStudio = 'C:\Program Files\Microsoft Visual Studio\2022\Community')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$probe = Join-Path $root 'build\decoder-host'
New-Item -ItemType Directory -Path $probe -Force | Out-Null
$fixtures = @(
    @{Name='pcm16.wav'; Rate='48000'; Codec='pcm_s16le'; Format='s16'; Duration='2'},
    @{Name='flac16.flac'; Rate='44100'; Codec='flac'; Format='s16'; Duration='2'},
    @{Name='flac24.flac'; Rate='96000'; Codec='flac'; Format='s32'; Duration='2'},
    @{Name='flac192k.flac'; Rate='192000'; Codec='flac'; Format='s32'; Duration='2'},
    @{Name='long.mp3'; Rate='48000'; Codec='libmp3lame'; Format='s16p'; Duration='480'}
)
foreach ($fixture in $fixtures) {
    $audioArgs = @('-hide_banner','-loglevel','error','-y','-f','lavfi','-i',("sine=frequency=440:sample_rate="+$fixture.Rate+":duration="+$fixture.Duration),'-ac','2','-c:a',$fixture.Codec,'-sample_fmt',$fixture.Format)
    if ($fixture.Codec -eq 'libmp3lame') { $audioArgs += @('-b:a','320k') }
    & ffmpeg @audioArgs (Join-Path $probe $fixture.Name)
    if ($LASTEXITCODE -ne 0) { throw ('Fixture generation failed: '+$fixture.Name) }
}
$batch = @"
@echo off
call "$VisualStudio\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /I"$root\third_party" "$root\native\decoder.cpp" "$root\native\decoder_hosttest.cpp" /Fe:decoder-hosttest.exe
"@
$batchPath = Join-Path $probe 'compile.cmd'
Set-Content -LiteralPath $batchPath -Value $batch -Encoding ascii
Push-Location $probe
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw 'Host decoder build failed' }
    $fixtureNames = $fixtures | ForEach-Object { $_.Name }
    & .\decoder-hosttest.exe @fixtureNames
    if ($LASTEXITCODE -ne 0) { throw 'Decoder regression failed' }
} finally { Pop-Location }
