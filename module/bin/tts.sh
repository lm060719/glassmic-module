#!/system/bin/sh
MODDIR=${0%/*}
DATA=/data/adb/glassmic
[ "$#" -ge 1 ] || { echo '{"ok":false,"error":"请输入文字"}'; exit 1; }
mkdir -p "$DATA/media"
name="TTS-$(date +%Y%m%d-%H%M%S)-$$.wav"
# Text is an argv value, never executable shell source.
engine=$(settings get secure tts_default_synth)
[ -n "$engine" ] && [ "$engine" != null ] || { echo '{"ok":false,"error":"请先在系统中配置语音引擎"}'; exit 1; }
temporary=$(mktemp /data/local/tmp/glassmic-tts.XXXXXXXX)
[ -n "$temporary" ] || { echo '{"ok":false,"error":"无法创建合成临时文件"}'; exit 1; }
trap 'rm -f "$temporary"' EXIT INT TERM
# TTS engines cannot write adb_data_file descriptors on every ROM. Give the
# engine a shell_data_file FD, then move the completed WAV as root.
result=$(CLASSPATH="$MODDIR/glassmic-tts.jar" /system/bin/app_process /system/bin io.mo.glassmic.tts.Main "$1" "$temporary" "$engine" "${2:-zh-CN}" "${3:-1}" "${4:-1}")
code=$?
if [ "$code" -ne 0 ]; then printf '%s\n' "$result"; exit "$code"; fi
mv "$temporary" "$DATA/media/$name" || { echo '{"ok":false,"error":"无法保存生成的语音"}'; exit 1; }
printf '{"ok":true,"file":"%s"}\n' "$name"
