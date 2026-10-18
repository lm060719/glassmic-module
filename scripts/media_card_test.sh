#!/system/bin/sh
# Device side of scripts/check_media_card.ps1; runs as root. Everything lives in this script's directory:
# a private test daemon, a copy of the module's bin/ and a private data directory. The installed module,
# its service and its data are never touched. Posts the real notification channel, which "remove" deletes.
#   auto     switch, controller commands, crash restart, then the uninstall path
#   manual   leaves the card showing for checks on the phone; finish with "remove"
#   remove   module remove flag: the card exits and the supervisor deletes the channel
DIR=${0%/*}
MOD=$DIR/module
BIN=$MOD/bin
export GLASSMIC_DATA=$DIR/data
CARD=$BIN/media-card.sh
KEY='0|android|27932|glassmic-media|1000'
failed=0
probe() { CLASSPATH=$BIN/glassmic-media.jar:$DIR/probe.jar app_process /system/bin io.mo.glassmic.overlay.MediaControl "$@"; }
ctl() { "$BIN/glassmicctl" --data "$GLASSMIC_DATA" "$@"; }
field() { grep -o "\"$1\":[^,}]*" | head -n 1 | cut -d: -f2; }
posted() { cmd notification list | grep -c 'glassmic-media'; }
channel() { probe channel; }
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; failed=1; fi; }
waitfor() { n=0; until eval "$1" || [ "$n" -ge "$2" ]; do sleep 0.5; n=$((n + 1)); done; }

setup() {
    chmod 0755 "$BIN/glassmicd" "$BIN/glassmicctl" "$CARD"
    rm -f "$MOD/remove" "$MOD/disable"
    rm -f "$GLASSMIC_DATA"/media-card.*
    mkdir -p "$GLASSMIC_DATA/media"
    cp "$DIR/test-tone.wav" "$GLASSMIC_DATA/media/test.wav"
    cp "$DIR/test-tone.wav" "$GLASSMIC_DATA/media/tone-2.wav"
    ctl stop > /dev/null 2>&1
    "$BIN/glassmicd" --data "$GLASSMIC_DATA" > "$DIR/daemon.log" 2>&1 < /dev/null &
    sleep 1
    ctl load test.wav > /dev/null
    "$CARD" supervise > "$DIR/supervisor.log" 2>&1 < /dev/null &
    waitfor '"$CARD" status | grep -q "\"supervisor\":true"' 10
    echo "status: $("$CARD" status)"
    check 'supervisor running, switch off' '"$CARD" status | grep -q "\"enabled\":false,\"running\":false,\"supervisor\":true"'
    echo "on: $("$CARD" on)"
    check 'switch on starts the card' '"$CARD" status | grep -q "\"enabled\":true,\"running\":true"'
    waitfor '[ "$(posted)" = 1 ]' 10
    check 'notification posted' '[ "$(posted)" = 1 ]'
    echo "channel: $(channel)"
    check 'dedicated low-importance channel of uid 1000' 'channel | grep -q "\"importance\":2,\"deleted\":false"'
    echo "substitute name: $(cmd notification get "$KEY" | grep -o 'android.substName=[^ ]*')"
    echo "settings file: $(cat "$GLASSMIC_DATA/media-card.conf"); leftover temp: $(ls "$GLASSMIC_DATA" | grep -c conf.tmp)"
}
remove() {
    touch "$MOD/remove"
    waitfor '! "$CARD" status | grep -q "\"supervisor\":true"' 20
    check 'remove flag stops card and supervisor' '"$CARD" status | grep -q "\"running\":false,\"supervisor\":false"'
    check 'notification removed' '[ "$(posted)" = 0 ]'
    echo "channel: $(channel)"
    check 'channel deleted' 'channel | grep -q "\"deleted\":true"'
    rm -f "$MOD/remove"
    ctl stop > /dev/null 2>&1
    echo "card log:"; cat "$GLASSMIC_DATA/media-card.log"
}

case "$1" in
auto)
    setup
    echo "info: $(probe info)"
    check 'session advertises seeking' 'probe info | grep -q "\"seekable\":true"'
    probe seek 1500 > /dev/null; sleep 1
    check 'seek from controller reaches daemon' '[ "$(ctl status | field positionMs)" -ge 1400 ] && [ "$(ctl status | field positionMs)" -le 1700 ]'
    probe play > /dev/null; sleep 1
    check 'play' 'ctl status | grep -q "\"paused\":false"'
    probe pause > /dev/null; sleep 1
    check 'pause' 'ctl status | grep -q "\"paused\":true"'
    probe next > /dev/null; sleep 1
    check 'next file' 'ctl status | grep -q "\"selected\":\"tone-2.wav\""'
    probe custom io.mo.glassmic.SILENCE > /dev/null; sleep 1
    check 'mute action' '[ "$(ctl status | field mode)" = 2 ]'
    probe custom io.mo.glassmic.REAL > /dev/null; sleep 1
    check 'real microphone action' '[ "$(ctl status | field mode)" = 0 ]'
    echo "off: $("$CARD" off)"
    check 'switch off stops the card' '"$CARD" status | grep -q "\"enabled\":false,\"running\":false"'
    check 'switch off removes the notification' '[ "$(posted)" = 0 ]'
    check 'card exited through the stop file' 'tail -n 1 "$GLASSMIC_DATA/media-card.log" | grep -q finished'
    "$CARD" on > /dev/null
    check 'switch on again' '"$CARD" status | grep -q "\"running\":true"'
    read -r pid < "$GLASSMIC_DATA/media-card.pid"
    kill -9 "$pid"
    waitfor '"$CARD" status | grep -q "\"running\":true" && [ "$(cat "$GLASSMIC_DATA/media-card.pid")" != "$pid" ]' 30
    check 'supervisor restarts a killed card' '"$CARD" status | grep -q "\"running\":true"'
    waitfor '[ "$(posted)" = 1 ]' 10
    check 'restarted card posts again' '[ "$(posted)" = 1 ]'
    remove
    [ "$failed" = 0 ] && echo 'MEDIA CARD TESTS PASSED' || echo 'MEDIA CARD TESTS FAILED'
    exit "$failed" ;;
manual) setup ;;
remove) remove ;;
*) echo 'usage: media_card_test.sh auto|manual|remove'; exit 2 ;;
esac
