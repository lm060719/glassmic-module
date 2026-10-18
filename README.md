# GlassMic · 璃麦
KernelSU 模块内的虚拟麦克风：WebUI 控制、独立原生音频引擎、Zygisk 录音后端。无需控制 APK，也不依赖 LSPosed。

最新构建为 0.1.0.8（未安装），新增控制中心媒体卡片：在 WebUI「设置」中打开后，可在通知栏和控制中心播放、暂停、拖动进度、切换音频、切回真实麦克风或静音，见 [悬浮控制方案](docs/floating-controls.md)。0.1.0.7 为 WebUI 增加了中英文切换。0.1.0.6 已安装到主测试机 25019PNF3C 并重启生效，微信的 hook 已装上（mask 15、stage 3）。主测试机（KernelSU）和 Mi 9T（Magisk）的 root 隔离测试均已全部通过；实际录音取音和长期稳定性仍待验证。该版本包含两项修复：
- 模块自带 ShadowHook 的 `libshadowhook_nothing.so`。此前只有 ROM 恰好提供这个库时才能装上 hook，否则后端完全不生效。
- 录音后端不再使用 `thread_local`，避免音频回调线程首次取音时在模拟 TLS 中分配内存和加锁。

0.1.0.4 修复播放进度依赖录音读取、FLAC 在文件选择器变灰、大文件逐块导入缓慢，以及 SELinux 拒绝共享音频 FD 导致 Hook 0 的问题，并兼容 Android 17 的 AudioRecord 字段改名。已在小米 15 Ultra / Android 17 完成隔离原生、SELinux 应用身份、直接导入与解码验证；停止旧版同时作用于微信后，用户确认录音恢复，新版实际取音计数增长。版本规则和基准记录见 [发版记录](docs/releases.md)。

## 安装和使用
需要 arm64-v8a、Android 10 以上、KernelSU，以及兼容系统的 Zygisk 实现。设备上已有 Zygisk Next 时无需重复安装。本模块不包含 Zygisk 运行时。

1. 在 KernelSU 中安装 `dist/glassmic-0.1.0.8-arm64.zip`，方便时重启一次。升级保留现有音频和设置。
2. 在模块列表打开璃麦的 WebUI。右上角按钮可切换中文 / English。
3. 在「设置 → 生效应用」选择目标应用并保存。首次选择的应用需要在下次正常启动时加载后端。
4. 点击「导入音频」，在模块文件选择页勾选内部存储中的 WAV / MP3 / FLAC。文件直接复制到音频库，原文件保留。云盘等位置可使用「系统文件选择器」。
5. 选择音频，点击播放，进度随播放前进；暂停保持进度。目标应用开始录音时接收当前播放位置的声音。此处供音不会通过手机扬声器播放。
6. 点击「麦克风」立即切回真实输入，或点击「静音」输出零 PCM。

使用本模块前，请在 LSPosed 中取消旧版 GlassMic 对目标应用的勾选，并在方便时重新启动该应用，使旧后端退出。旧版与新版同时作用于微信时，实机曾出现“语音被其他应用占用，或因系统错误”的提示；取消旧版后恢复。可以保留旧 APK 的文件与设置，以及其他 LSPosed 模块；本模块自身不需要 LSPosed。

重启设备或音频服务异常重启后，默认回到真实麦克风，保留文件、选曲和设置，需要手动点击播放重新开启虚拟输入。关闭 WebUI 不会停止原生供音。

## WebUI 功能
- 手机文件直接导入、跨文件夹多选和文件名筛选；系统选择器备用导入；单文件最大 512 MiB。只删除模块中的副本，不删除原始文件。
- 播放、暂停、Seek、循环、真实麦克风和静音切换。
- 输出增益 -24 至 +12 dB、软限幅、0.5 至 2 倍播放速度。变速会同时改变音调。
- 系统 TTS 合成为 WAV：普通话、English、中文（台湾），要求手机已配置引擎及相应语言数据。TTS 会话服务不可用的旧系统会返回明确错误。
- 控制中心卡片开关：默认关闭；打开后由模块的独立进程注册媒体会话，开机后按开关自动显示，关闭即移除，不需要 APK。
- 生效应用白名单、连接状态、初始化阶段及错误码、读取/欠载/跳帧诊断。
- 浅色/深色、200% 字号适配。页面所有资源离线打包，无网络服务器或远程页面。

TTS 合成过程中保持页面打开。TTS 使用系统会话服务绑定语音引擎，不打开 Activity；引擎是否联网由其自身设置决定。

## 构建
本机配置为 JDK 17、Android SDK 35、NDK 26.1.10909125、CMake 3.22.1。构建脚本支持通过参数指定 SDK/JDK/NDK 路径。

```powershell
.\scripts\build.ps1
```

首次构建下载固定版本的 miniaudio、ShadowHook、Zygisk API 4 头文件和 KernelSU JS API。来源及 SHA-256 固定在 `dependencies.lock.json`，后续构建校验缓存。输出为单个模块 ZIP 和校验文件，不生成 APK。

## 验证
```powershell
# 对已连接设备运行隔离测试。只启动/停止本项目创建的测试进程。
python scripts/device_check.py
```

测试数据放在 `/data/local/tmp/glassmic-dev-20261013/`，不会启动游戏、采集真实麦克风、改变手机界面或重启 Android 服务。测试会构造消费者验证 PCM、FD 传输、重采样、录音心跳、Seek、白名单更新、服务恢复和 UID 降权后的映射读取。

控制中心卡片：`.\scripts\check_media_card.ps1 -Serial <设备>` 在同一隔离目录测试开关、监督、控制命令和卸载路径（会在手机上短暂显示通知，先征得同意）。

WebUI 的桌面预览需要模拟 KernelSU 桥接；`scripts/mock_webui.js` 仅用于开发，不进入模块。真实管理器内的文件选择和 Zygisk 应用生命周期仍需设备交互验证。

仅在 Windows 本地验证解码（需要 Visual Studio C++ 编译工具和 FFmpeg，不使用 ADB）：
```powershell
.\scripts\host_decoder_check.ps1
```

0.1.0.4 使用 ShadowHook 2.0.1；该版本的[上游发布说明](https://github.com/bytedance/android-inline-hook/releases/tag/v2.0.1)明确包含 Android 17 兼容性更新。手机 AVC 日志确认此前 Hook 0 的直接原因是共享文件被标为 `device`，Zygote 无权读取；专用文件类型的修复已在隔离进程的 Zygote → untrusted_app_34 路径验证。真实微信已验证后端加载和实际取音，长期连续录音及其他应用兼容性仍需验证。

见 [验证记录](docs/validation.md) 和 [实现说明](docs/architecture.md)。

## 数据与卸载
- `/data/adb/glassmic/media/`：导入音频及 TTS 结果。
- `/data/adb/glassmic/`：设置、白名单、选曲、控制 socket 与日志；`media-card.conf` 为控制中心卡片开关。
- `/dev/glassmic/`：共享 PCM 与消费者状态。位于 tmpfs，不持续写入闪存，设备重启后清空。
- `/data/adb/modules/glassmic/targets.txt`：给加载器快速筛选应用的白名单副本，不包含音频。

卸载保留音频和设置，避免误删用户文件。控制中心卡片的通知渠道（`android` 包下的 `glassmic.media`）在卸载时删除。首次安装白名单为空，不会自动作用到任何游戏或应用。

许可证：GPL-3.0；第三方组件保留各自许可证，见 `module/licenses/`。
