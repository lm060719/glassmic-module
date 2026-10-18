param([Parameter(Mandatory=$true)][string]$Serial,[ValidateSet('auto','manual','remove','log')][string]$Action='auto')
# Control-center media card in an isolated device directory (scripts/media_card_test.sh does the work).
# Uses the module files from scripts/build.ps1 with a private test daemon and data directory; the installed
# module is untouched. Posts a real notification and channel (ask the user first); "remove" deletes them.
#   auto    automated checks, ending with the uninstall path
#   manual  leaves the card showing for checks on the phone; finish with -Action remove
# Native tools write progress and notes to stderr; every step checks its exit code instead.
$ErrorActionPreference='Continue'
$root=Split-Path -Parent $PSScriptRoot
$java='C:\Program Files\Java\jdk-17.0.0.1'
$android='E:\Android\Sdk\platforms\android-35\android.jar'
$output=Join-Path $root 'build\media-test'
$dir='/data/local/tmp/glassmic-dev-20261013/media-card'
function Device([string]$command){adb -s $Serial shell "su -c '$command'"}
if($Action -eq 'log'){Device "cat $dir/data/media-card.log; cat $dir/supervisor.log";return}
if($Action -ne 'remove'){
    $bin=Join-Path $root 'build\module\bin'
    if(-not (Test-Path (Join-Path $bin 'glassmic-media.jar'))){throw 'Run scripts/build.ps1 first'}
    Remove-Item -Recurse -Force $output -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path (Join-Path $output 'classes') -Force | Out-Null
    $sources=@((Get-ChildItem (Join-Path $root 'overlay\*.java')).FullName)+(Join-Path $root 'scripts\probes\MediaControl.java')
    & (Join-Path $java 'bin\javac.exe') -encoding UTF-8 -source 17 -target 17 -nowarn -cp $android -d (Join-Path $output 'classes') $sources
    if($LASTEXITCODE -ne 0){throw 'Probe Java build failed'}
    # Only the probe class; the card classes come from the module's own DEX.
    & (Join-Path $java 'bin\jar.exe') cf (Join-Path $output 'probe-classes.jar') -C (Join-Path $output 'classes') 'io/mo/glassmic/overlay/MediaControl.class'
    $env:JAVA_HOME=$java
    & 'E:\Android\Sdk\build-tools\35.0.0\d8.bat' --min-api 29 --lib $android --classpath (Join-Path $output 'classes') --output (Join-Path $output 'probe.jar') (Join-Path $output 'probe-classes.jar')
    if($LASTEXITCODE -ne 0){throw 'Probe dex build failed'}
    $script=Join-Path $output 'media_card_test.sh'
    [IO.File]::WriteAllText($script,([IO.File]::ReadAllText((Join-Path $root 'scripts\media_card_test.sh')) -replace "`r`n","`n"))
    adb -s $Serial shell "mkdir -p $dir/module/bin"
    foreach($file in 'glassmicd','glassmicctl','media-card.sh','glassmic-media.jar'){
        adb -s $Serial push (Join-Path $bin $file) "$dir/module/bin/" | Out-Null
        if($LASTEXITCODE -ne 0){throw "Upload failed: $file"}
    }
    foreach($file in (Join-Path $output 'probe.jar'),$script,(Join-Path $root 'build\test-tone.wav')){
        adb -s $Serial push $file "$dir/" | Out-Null
        if($LASTEXITCODE -ne 0){throw "Upload failed: $file"}
    }
    Device "chmod 0755 $dir/media_card_test.sh"
}
Device "pidof glassmicd"
Device "$dir/media_card_test.sh $Action"
$code=$LASTEXITCODE
Device "pidof glassmicd"
if($code -ne 0){throw "Media card check failed ($code)"}
