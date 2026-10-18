param([Parameter(Mandatory=$true)][string]$Serial,[ValidateSet('start','log','stop')][string]$Action='start',[string]$Lang='zh',[int]$Seconds=600)
# Control-center media card against the private test daemon. Posts a notification (needs the user's
# permission). Run scripts/check_overlay_prototype.ps1 first to build and upload the DEX.
$ErrorActionPreference='Continue'
$dir='/data/local/tmp/glassmic-dev-20261013/overlay'
$state="$dir/state"
$ctl="$dir/glassmicctl --data $state"
$run="app_process /system/bin io.mo.glassmic.overlay.MediaCard --ctl $dir/glassmicctl --data $state"
if($Action -eq 'log'){adb -s $Serial shell "su -c 'cat $dir/media-card.log'";return}
if($Action -eq 'stop'){
    # The probe removes its notification and session when the stop file appears.
    adb -s $Serial shell "su -c 'touch $state/media-card.stop'"
    Start-Sleep -Seconds 3
    # pkill runs alone: any command line naming the class (such as --cancel below) would match its own shell.
    adb -s $Serial shell "su -c 'pkill -f [i]o.mo.glassmic.overlay.MediaCard'"
    # --cancel covers a probe that did not exit cleanly.
    adb -s $Serial shell "su -c 'CLASSPATH=$dir/glassmic-overlay.jar $run --cancel; $ctl stop'"
    adb -s $Serial shell "su -c 'cat $dir/media-card.log; pidof glassmicd'"
    return
}
adb -s $Serial shell "su -c 'mkdir -p $state/media; cp /data/local/tmp/glassmic-dev-20261013/test-tone.wav $state/media/test.wav; cp /data/local/tmp/glassmic-dev-20261013/test-tone.wav $state/media/tone-2.wav'"
# Only the private development daemon and this probe process are started; stop a leftover test daemon first.
adb -s $Serial shell "su -c '$ctl stop'" | Out-Null
adb -s $Serial shell "su -c '$dir/glassmicd --data $state > $dir/daemon.log 2>&1 < /dev/null &'"
Start-Sleep -Milliseconds 500
adb -s $Serial shell "su -c 'CLASSPATH=$dir/glassmic-overlay.jar nohup $run --lang $Lang --seconds $Seconds > $dir/media-card.log 2>&1 < /dev/null &'"
Start-Sleep -Seconds 3
adb -s $Serial shell "su -c 'cat $dir/media-card.log'"
