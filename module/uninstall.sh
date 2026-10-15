#!/system/bin/sh
# Preserve imported media and settings.
MODDIR=${0%/*}
"$MODDIR/bin/glassmicctl" stop >/dev/null 2>&1
