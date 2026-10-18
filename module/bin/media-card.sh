#!/system/bin/sh
# Control-center media card: switch, boot supervisor and cleanup.
#   status | on | off   print one JSON line for the WebUI
#   supervise           started by service.sh; runs the card while the switch is on
#   uninstall           removes the notification and deletes its channel
BIN=${0%/*}
MODDIR=${BIN%/*}
# GLASSMIC_DATA points isolated tests at a private directory.
DATA=${GLASSMIC_DATA:-/data/adb/glassmic}
CONF=$DATA/media-card.conf
STOP=$DATA/media-card.stop
PIDFILE=$DATA/media-card.pid
SUPERVISOR=$DATA/media-card-supervisor.pid
LOG=$DATA/media-card.log
NAME=glassmic-media
CLASS=io.mo.glassmic.overlay.MediaCard

card() { CLASSPATH="$BIN/glassmic-media.jar" /system/bin/app_process /system/bin "$@"; }
enabled() { { read -r line < "$CONF"; } 2>/dev/null && [ "$line" = enabled=1 ]; }
active() { [ -d "$MODDIR" ] && [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; }
# A PID file is trusted only while that process still carries the expected name, so only our own processes are
# signalled. The card is matched by its --nice-name (cmdline; ART renames its main thread "main" in comm).
owned() {
    # cmdline has no trailing newline, so read reports EOF even though it filled in the name.
    pid= name=
    { read -r pid < "$1"; read -r name < "/proc/$pid/$3"; } 2>/dev/null
    [ -n "$pid" ] && [ "$name" = "$2" ]
}
running() { owned "$PIDFILE" "$NAME" cmdline; }
supervised() { owned "$SUPERVISOR" media-card.sh comm; }
# The channel is filed under the "android" package's uid 1000, and the system deletes only the caller's own
# channels, so this step runs as uid 1000 from a copy of the DEX that uid can read.
uninstall() {
    jar=$(mktemp /data/local/tmp/glassmic-media.XXXXXXXX) || return 1
    cp "$BIN/glassmic-media.jar" "$jar" && chmod 0644 "$jar" &&
        su 1000 -c "CLASSPATH=$jar /system/bin/app_process /system/bin $CLASS --uninstall"
    code=$?
    rm -f "$jar"
    return "$code"
}
# Writes the setting to a temporary file and renames it over the old one, so readers never see a partial file.
save() { mkdir -p "$DATA" && printf 'enabled=%s\n' "$1" > "$CONF.tmp" && mv -f "$CONF.tmp" "$CONF"; }
bool() { "$@" && printf true || printf false; }
status() { printf '{"ok":true,"enabled":%s,"running":%s,"supervisor":%s}\n' "$(bool enabled)" "$(bool running)" "$(bool supervised)"; }
fail() { printf '{"ok":false,"error":"%s"}\n' "$1"; exit 1; }

supervise() {
    supervised && fail 'supervisor already running'
    echo "$$" > "$SUPERVISOR"
    until [ "$(getprop sys.boot_completed)" = 1 ] || ! active; do sleep 2; done
    : > "$LOG"
    quick=0
    while active; do
        # Cleared before reading the switch: "off" saves the switch first and then creates the stop file,
        # so either the card is not started or it sees the stop file and exits.
        [ -e "$STOP" ] && rm -f "$STOP"
        if ! enabled; then quick=0; sleep 2; continue; fi
        started=$(date +%s)
        # A simple command, not the card function: $! must be app_process itself, not a subshell.
        CLASSPATH="$BIN/glassmic-media.jar" /system/bin/app_process /system/bin --nice-name=$NAME $CLASS \
            --ctl "$BIN/glassmicctl" --data "$DATA" --module "$MODDIR" >> "$LOG" 2>&1 < /dev/null &
        pid=$!
        echo "$pid" > "$PIDFILE"
        wait "$pid"
        code=$?
        rm -f "$PIDFILE"
        [ "$code" -eq 0 ] && continue
        # Killed or crashed: the notification outlives the process.
        card $CLASS --cancel >> "$LOG" 2>&1 < /dev/null
        if [ $(( $(date +%s) - started )) -lt 60 ]; then quick=$((quick + 1)); else quick=0; fi
        delay=5; [ "$quick" -ge 5 ] && delay=300
        while [ "$delay" -gt 0 ] && active && enabled; do sleep 2; delay=$((delay - 2)); done
    done
    [ -f "$MODDIR/remove" ] && uninstall >> "$LOG" 2>&1 < /dev/null
    rm -f "$SUPERVISOR"
}

case "$1" in
status) status ;;
on)
    save 1 || fail 'cannot save media card setting'
    # The supervisor checks the switch every 2 seconds; app_process needs about a second more.
    n=0; while supervised && ! running && [ "$n" -lt 25 ]; do sleep 0.2; n=$((n + 1)); done
    status ;;
off)
    save 0 || fail 'cannot save media card setting'
    : > "$STOP"
    n=0; while running && [ "$n" -lt 25 ]; do sleep 0.2; n=$((n + 1)); done
    if running; then
        kill "$pid"; sleep 1
        card $CLASS --cancel >/dev/null 2>&1 < /dev/null
    fi
    status ;;
supervise) supervise ;;
uninstall) uninstall ;;
*) fail 'usage: media-card.sh status|on|off|supervise|uninstall' ;;
esac
