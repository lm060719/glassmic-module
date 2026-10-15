param([Parameter(Mandatory=$true)][string]$Serial)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$taskJava='C:\Program Files\Java\jdk-17.0.0.1'
$output=Join-Path $root 'build\overlay-probe'
New-Item -ItemType Directory -Path $output -Force | Out-Null
& (Join-Path $taskJava 'bin\javac.exe') -encoding UTF-8 -cp 'E:\Android\Sdk\platforms\android-35\android.jar' -d $output (Join-Path $PSScriptRoot 'probes\OverlayEnvironment.java')
if($LASTEXITCODE -ne 0){throw 'Overlay capability probe compile failed'}
& (Join-Path $taskJava 'bin\jar.exe') cf (Join-Path $output 'classes.jar') -C $output OverlayEnvironment.class
if($LASTEXITCODE -ne 0){throw 'Overlay capability probe jar failed'}
$env:JAVA_HOME=$taskJava
& 'E:\Android\Sdk\build-tools\35.0.0\d8.bat' --min-api 29 --lib 'E:\Android\Sdk\platforms\android-35\android.jar' --output (Join-Path $output 'probe.jar') (Join-Path $output 'classes.jar')
if($LASTEXITCODE -ne 0){throw 'Overlay capability probe dex failed'}
$remote='/data/local/tmp/glassmic-dev-20261013/overlay-probe.jar'
adb -s $Serial push (Join-Path $output 'probe.jar') $remote
if($LASTEXITCODE -ne 0){throw 'Overlay capability probe upload failed'}
# This probe never adds a window or launches an Activity.
adb -s $Serial shell "su -c 'CLASSPATH=$remote app_process /system/bin OverlayEnvironment'"
if($LASTEXITCODE -ne 0){throw 'Overlay capability probe failed'}
