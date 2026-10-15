#!/system/bin/sh
MODDIR=${0%/*}
DATA=/data/adb/glassmic
mkdir -p "$DATA/media"
chmod 0700 "$DATA" "$DATA/media"
while [ -d "$MODDIR" ] && [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; do
    "$MODDIR/bin/glassmicd" --data "$DATA" >> "$DATA/daemon.log" 2>&1
    code=$?
    [ "$code" -eq 73 ] && exit 0
    sleep 5
done
