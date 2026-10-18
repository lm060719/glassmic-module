param([Parameter(Mandatory=$true)][string]$Serial,[ValidateSet('start','stop')][string]$Action='start',[string]$Lang='zh')
# Shows the floating controls on the phone against the private test daemon. Needs the user's
# permission (it draws on screen). Run scripts/check_overlay_prototype.ps1 first to build and upload.
$ErrorActionPreference='Continue'
$dir='/data/local/tmp/glassmic-dev-20261013/overlay'
$state="$dir/state"
$ctl="$dir/glassmicctl --data $state"
if($Action -eq 'stop'){
    # Only this task's overlay matches; the bracket keeps pkill from matching its own shell.
    adb -s $Serial shell "su -c 'pkill -f [i]o.mo.glassmic.overlay.Main; $ctl stop'"
    adb -s $Serial shell "su -c 'cat $dir/overlay.log; pidof glassmicd'"
    return
}
adb -s $Serial shell "su -c 'mkdir -p $state/media; cp /data/local/tmp/glassmic-dev-20261013/test-tone.wav $state/media/test.wav'"
# Only the private development daemon and this overlay process are started.
adb -s $Serial shell "su -c '$dir/glassmicd --data $state > $dir/daemon.log 2>&1 < /dev/null &'"
Start-Sleep -Milliseconds 500
adb -s $Serial shell "su -c 'CLASSPATH=$dir/glassmic-overlay.jar nohup app_process /system/bin io.mo.glassmic.overlay.Main --show --lang $Lang --ctl $dir/glassmicctl --data $state > $dir/overlay.log 2>&1 < /dev/null &'"
Start-Sleep -Seconds 2
adb -s $Serial shell "su -c 'cat $dir/overlay.log'"
