param([Parameter(Mandatory=$true)][string]$Serial,[string[]]$Modes=@('state','paint','textview','preload-paint','preload-textview'))
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$taskJava='C:\Program Files\Java\jdk-17.0.0.1'
$android='E:\Android\Sdk\platforms\android-35\android.jar'
$output=Join-Path $root 'build\overlay-text'
New-Item -ItemType Directory -Path $output -Force | Out-Null
& (Join-Path $taskJava 'bin\javac.exe') -encoding UTF-8 -cp $android -d $output (Join-Path $PSScriptRoot 'probes\OverlayText.java')
if($LASTEXITCODE -ne 0){throw 'Overlay text probe compile failed'}
& (Join-Path $taskJava 'bin\jar.exe') cf (Join-Path $output 'classes.jar') -C $output OverlayText.class
if($LASTEXITCODE -ne 0){throw 'Overlay text probe jar failed'}
$env:JAVA_HOME=$taskJava
& 'E:\Android\Sdk\build-tools\35.0.0\d8.bat' --min-api 29 --lib $android --output (Join-Path $output 'probe.jar') (Join-Path $output 'classes.jar')
if($LASTEXITCODE -ne 0){throw 'Overlay text probe dex failed'}
$dir='/data/local/tmp/glassmic-dev-20261013/overlay'
adb -s $Serial shell "mkdir -p $dir"
adb -s $Serial push (Join-Path $output 'probe.jar') "$dir/text-probe.jar" | Out-Null
if($LASTEXITCODE -ne 0){throw 'Overlay text probe upload failed'}
# Each mode runs in its own process: a native font assertion aborts the process.
# The probe never adds a window or launches an Activity.
$ErrorActionPreference='Continue'
foreach($mode in $Modes){
    Write-Output "=== $mode"
    $since=(adb -s $Serial shell "date '+%m-%d %H:%M:%S.000'").Trim()
    adb -s $Serial shell "su -c 'CLASSPATH=$dir/text-probe.jar app_process /system/bin OverlayText $mode $dir/$mode.png 2>&1'"
    Write-Output "exit=$LASTEXITCODE"
    adb -s $Serial shell "su -c 'logcat -d -T \`"$since\`" -b crash -b main -b system'" | Select-String -Pattern 'Abort message|F DEBUG|FATAL|minikin|OverlayText|AndroidRuntime: (?!Calling)' | Select-Object -First 25 | ForEach-Object { $_.Line }
    adb -s $Serial pull "$dir/$mode.png" (Join-Path $output "$mode.png") 2>$null | Out-Null
}
