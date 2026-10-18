#!/system/bin/sh
MODDIR=${0%/*}
DATA=/data/adb/glassmic
mkdir -p "$DATA/media"
chmod 0700 "$DATA" "$DATA/media"
# Control-center card, off unless enabled in the WebUI.
"$MODDIR/bin/media-card.sh" supervise < /dev/null > /dev/null 2>&1 &
while [ -d "$MODDIR" ] && [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; do
    "$MODDIR/bin/glassmicd" --data "$DATA" >> "$DATA/daemon.log" 2>&1
    code=$?
    [ "$code" -eq 73 ] && exit 0
    sleep 5
done
