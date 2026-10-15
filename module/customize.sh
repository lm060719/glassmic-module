#!/system/bin/sh
[ "$ARCH" = arm64 ] || abort "GlassMic requires arm64-v8a"
[ "$API" -ge 29 ] || abort "Android 10 or newer required"
ui_print "GlassMic: native daemon + Zygisk + local WebUI"
ui_print "A compatible Zygisk implementation is required."
set_perm_recursive "$MODPATH/bin" 0 0 0755 0755
set_perm_recursive "$MODPATH/zygisk" 0 0 0755 0644
set_perm_recursive "$MODPATH/lib" 0 0 0755 0644
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
# KernelSU owns webroot permissions/context; do not override them.
