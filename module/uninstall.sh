#!/system/bin/sh
# Preserve imported media and settings.
MODDIR=${0%/*}
DATA=/data/adb/glassmic
"$MODDIR/bin/glassmicctl" stop >/dev/null 2>&1
# The media card's notification channel lives in system notification settings. The card supervisor deletes it
# when the remove flag appears, but KernelSU runs this script early in boot, before system_server takes
# notification calls, so also delete it once boot completes. The channel belongs to the "android" package's
# uid 1000 and only that uid can delete it, so a copy of the DEX goes where uid 1000 can read it.
if [ -f "$DATA/media-card.conf" ] && jar=$(mktemp /data/local/tmp/glassmic-media.XXXXXXXX); then
    if cp "$MODDIR/bin/glassmic-media.jar" "$jar" && chmod 0644 "$jar"; then
        (
            n=0
            until [ "$(getprop sys.boot_completed)" = 1 ] || [ "$n" -ge 300 ]; do sleep 2; n=$((n + 1)); done
            su 1000 -c "CLASSPATH=$jar /system/bin/app_process /system/bin io.mo.glassmic.overlay.MediaCard --uninstall"
            rm -f "$jar"
        ) < /dev/null > /dev/null 2>&1 &
    else
        rm -f "$jar"
    fi
fi
