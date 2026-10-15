param([Parameter(Mandatory=$true)][string]$Serial)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$java='C:\Program Files\Java\jdk-17.0.0.1'
$output=Join-Path $root 'build\record-fields'
New-Item -ItemType Directory -Path $output -Force | Out-Null
& (Join-Path $java 'bin\javac.exe') -encoding UTF-8 -d $output (Join-Path $PSScriptRoot 'probes\AudioRecordFields.java')
if($LASTEXITCODE -ne 0){throw 'Metadata probe compile failed'}
& (Join-Path $java 'bin\jar.exe') cf (Join-Path $output 'classes.jar') -C $output AudioRecordFields.class
$env:JAVA_HOME=$java
& 'E:\Android\Sdk\build-tools\35.0.0\d8.bat' --min-api 29 --output (Join-Path $output 'probe.jar') (Join-Path $output 'classes.jar')
if($LASTEXITCODE -ne 0){throw 'Metadata probe dex failed'}
$remote='/data/local/tmp/glassmic-dev-20261013/record-fields.jar'
adb -s $Serial push (Join-Path $output 'probe.jar') $remote
if($LASTEXITCODE -ne 0){throw 'Metadata probe upload failed'}
adb -s $Serial shell "su -c 'CLASSPATH=$remote app_process /system/bin AudioRecordFields'"
if($LASTEXITCODE -ne 0){throw 'Metadata probe failed'}
