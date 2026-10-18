param([Parameter(Mandatory=$true)][string]$Serial,[string]$Lang='zh')
# Offscreen check of the floating controls against a private test daemon.
# Renders PNGs and clicks controls in-process; never adds a window or touches the installed module.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$java='C:\Program Files\Java\jdk-17.0.0.1'
$android='E:\Android\Sdk\platforms\android-35\android.jar'
$output=Join-Path $root 'build\overlay'
Remove-Item -Recurse -Force (Join-Path $output 'classes') -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path (Join-Path $output 'classes'),(Join-Path $output 'render') -Force | Out-Null
& (Join-Path $java 'bin\javac.exe') -encoding UTF-8 -source 17 -target 17 -cp $android -d (Join-Path $output 'classes') (Join-Path $root 'overlay\Main.java') (Join-Path $root 'overlay\MediaCard.java')
if($LASTEXITCODE -ne 0){throw 'Overlay Java build failed'}
& (Join-Path $java 'bin\jar.exe') cf (Join-Path $output 'classes.jar') -C (Join-Path $output 'classes') .
$env:JAVA_HOME=$java
& 'E:\Android\Sdk\build-tools\35.0.0\d8.bat' --min-api 29 --lib $android --output (Join-Path $output 'glassmic-overlay.jar') (Join-Path $output 'classes.jar')
if($LASTEXITCODE -ne 0){throw 'Overlay dex build failed'}

$dir='/data/local/tmp/glassmic-dev-20261013/overlay'
$state="$dir/state"
adb -s $Serial shell "mkdir -p $dir/render"
foreach($file in (Join-Path $output 'glassmic-overlay.jar'),(Join-Path $root 'build\native\glassmicd'),(Join-Path $root 'build\native\glassmicctl')){
    adb -s $Serial push $file "$dir/" | Out-Null
    if($LASTEXITCODE -ne 0){throw "Upload failed: $file"}
}
$ctl="$dir/glassmicctl --data $state"
adb -s $Serial shell "su -c 'chmod 755 $dir/glassmicd $dir/glassmicctl; mkdir -p $state/media; cp /data/local/tmp/glassmic-dev-20261013/test-tone.wav $state/media/test.wav; rm -f $dir/render/*'"
# Starts only the private development daemon.
adb -s $Serial shell "su -c '$dir/glassmicd --data $state > $dir/daemon.log 2>&1 < /dev/null &'"
$ErrorActionPreference='Continue'
try {
    Start-Sleep -Milliseconds 500
    adb -s $Serial shell "su -c '$ctl status'" | Out-Null
    adb -s $Serial shell "su -c 'CLASSPATH=$dir/glassmic-overlay.jar app_process /system/bin io.mo.glassmic.overlay.Main --render $dir/render --exercise --lang $Lang --ctl $dir/glassmicctl --data $state 2>&1'"
    $code=$LASTEXITCODE
} finally {
    adb -s $Serial shell "su -c '$ctl stop'" | Out-Null
}
foreach($name in 'balls','panel-light','panel-dark','panel-playing'){adb -s $Serial pull "$dir/render/$name.png" (Join-Path $output "render\$name.png") 2>$null | Out-Null}
adb -s $Serial shell "su -c 'pidof glassmicd'"
if($code -ne 0){throw "Overlay offscreen check failed ($code)"}
