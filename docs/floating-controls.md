# 悬浮控制方案与兼容性检查

## 目标体验

保持无控制 APK，由模块携带独立的 Android 浮窗程序。WebUI 负责导入、TTS 和生效应用设置；浮球用于其他应用使用期间的快速控制。

- 收起为可拖动的小球，点击展开，记住位置；提供明确的关闭入口。
- 展开后显示当前文件、进度、播放/暂停、音频列表、真实麦克风和静音切换。
- 与 WebUI 共用现有原生音频引擎及设置，定期同步状态。关闭浮窗继续供音，浮窗不安装音频 Hook。
- 初次默认关闭，由用户从 WebUI 启用。录音回调不依赖浮窗，不引入旧版 LSPosed 后端。

## 技术边界

[KernelSU WebUI](https://github.com/tiann/KernelSU/blob/main/website/docs/guide/module-webui.md) 由管理器的 WebView 承载。跨应用浮窗需要模块中的额外界面进程，HTML 页面中的悬浮布局本身不会跨出管理器。

建议使用模块内 DEX 经 `app_process` 启动，Android WindowManager 绘制原生浮层。控制请求交给工作线程调用 `glassmicctl`；主线程只负责显示与手势。窗口不抢前台焦点，只响应自身区域的触摸。重启后不自动显示，也不恢复虚拟输入。

这条路线使用 Android 系统接口，需要针对 ROM 验证，不能仅凭 Root 权限保证兼容所有设备。浮窗原型见下文，未纳入模块；最终纳入模块的是控制中心媒体卡片（0.1.0.8）。

## 小米 15 Ultra / Android 17 检查结果

2026-10-14，仅使用授权的 25019PNF3C，无线 ADB 明确指定设备，运行本项目独立检查进程；没有显示窗口、跳转应用、打开麦克风或重启现有进程。

`scripts/probes/OverlayEnvironment.java` 的最终检查通过：UID 0；WindowManager 可取得；INTERNAL_SYSTEM_WINDOW 权限允许（返回 0）；普通 View 离屏绘制成功；windowDisplayed 为 false。原生音频服务 PID 3298 保持运行。

Android 17 的 WindowManager 在独立进程中要求初始化 ApplicationSharedMemory。检查使用进程内的新实例；没有改动系统进程或系统的共享内存。之前缺少初始化时，检查进程失败，加入初始化后通过。

标准 TextView 的首次文字绘制触发了本 ROM 的原生字体初始化断言。最终探针改为普通 View，以隔离验证权限与基础绘制；文字渲染问题仍需适配。跨应用真实窗口添加、拖拽和点击、字体缩放、横竖屏、锁屏隐藏、内存占用及进程退出也尚未验证。

只有完成上述兼容性处理，才将浮窗功能纳入模块。实际显示测试窗口前，依照用户约定取得对手机界面操作的许可。

## 文字渲染与原型（2026-10-18）

`scripts/check_overlay_text.ps1` 在隔离目录中逐一启动独立进程测试离屏文字绘制（`scripts/probes/OverlayText.java`）：

| 方式 | 结果 |
|---|---|
| Paint.drawText | 崩溃：`Assertion failed: src == nullptr && gDefaultTypeface == nullptr` |
| TextView | 同上 |
| 先调用 `Typeface.loadPreinstalledSystemFontMap()` 再绘制 | 两种方式均成功，中英文、符号和数字都使用系统字体 |

原因：Android 17 开启了 `ENABLE_LAZY_TYPEFACE_INITIALIZATION`，普通应用的默认字体由 Zygote 或 bindApplication 设置，裸 `app_process` 进程从未设置，第一次排版时 Minikin 就会断言。原型启动时若 `sDefaultTypeface` 为空，就加载一次预装字体表。

原型位于 `overlay/Main.java`（`io.mo.glassmic.overlay.Main`），尚未纳入模块打包。离屏检查中还发现并处理了两个问题：

- 点击时 `View.performClick` 会创建 AutofillManager，而 HyperOS 的 `ViewRootImplStubImpl` 还会直接读取 `Settings.Secure`。这个进程没有在 ActivityManager 注册，Settings 提供者会拒绝访问，所以和系统自带的 `settings` 命令一样，用 `getContentProviderExternal` 获取 Settings 提供者并填入 Settings 的缓存；AutofillManager 直接返回空。
- 单行（singleLine）加居中的 TextView 要等预绘制阶段才会把文字滚回可见区域，离屏绘制时文字会跑到画面外。按钮改用 `maxLines(1)`。

`scripts/check_overlay_prototype.ps1` 在隔离目录启动私有测试守护进程，原型以 `--render --exercise` 运行：渲染浅色/深色面板及 5 种浮球状态的 PNG，再调用按钮的 `performClick` 依次执行选曲、播放、暂停、静音、真实麦克风和收起/展开，通过守护进程的状态确认每一步。中英文均通过，`windowAdded` 为 false，正式服务 PID 不变。

## 真实窗口：被系统拒绝（2026-10-18）

经用户同意，`scripts/check_overlay_window.ps1` 以 `--show` 启动原型，只连接隔离目录中的测试守护进程。`WindowManager.addView` 在建立窗口会话时失败，屏幕上没有显示任何内容：

```
java.lang.IllegalStateException: Unknown pid=4816 uid=0
    at android.view.IWindowManager$Stub$Proxy.openSession
```

反汇编设备上 `services.jar` 中的 `com.android.server.wm.Session.<init>` 可以确认：构造函数先查找 `mAtmService.mProcessMap.getProcess(callingPid)`，找不到就直接抛出这个异常，没有按 UID 或权限放行的分支。`WindowProcessControllerMap` 只登记由 ActivityManager 启动的应用进程，因此 `app_process` 起的 root 进程在这台设备（HyperOS 4 / Android 17）上无法添加任何窗口；把 UID 换成 system 也绕不过去。离屏渲染、字体和控制逻辑都不受影响，问题只在窗口由哪个进程承载。

可选方向（均未实现，需要用户决定）：

1. 承载进程改为由 ActivityManager 启动的进程，例如通过 Zygisk 在指定的系统应用进程里加载浮窗 DEX，root 控制仍由独立进程负责。这涉及向运行中的应用加载代码，需要用户明确同意。
2. 模块附带一个只用来承载浮窗的小 APK。可靠性最好，但与“无 APK”的原则冲突。
3. 不使用窗口：注册 MediaSession，在通知栏/控制中心的媒体卡片里提供播放、暂停等按钮。已验证可行，见下文。
4. 不做跨应用浮窗：为 WebUI 增加适合小窗的紧凑布局，配合 HyperOS 自由小窗和 KernelSU 的 action 按钮。

## 控制中心媒体卡片（2026-10-18）

`overlay/MediaCard.java`（`io.mo.glassmic.overlay.MediaCard`）由 root `app_process` 创建 MediaSession，并以 `android` 包发出 MediaStyle 通知；不添加窗口，也不需要 APK。卡片上的操作以 MediaSession 回调的形式到达，在工作线程调用 `glassmicctl`：播放/暂停 → `play`/`pause`，上一首/下一首 → 按名称顺序 `load`，拖动进度 → `seek`，两个自定义操作 → `real`/`silence`。每秒读取一次状态，更新标题（当前文件）、副标题（输入来源）、时长和播放状态。

启动时还需调用 `ActivityThread.initializeMainlineModules()`，否则 `MediaSessionManager` 拿不到 Mainline 服务注册器，会抛出空指针异常。

经用户同意，`scripts/check_media_card.ps1` 在 25019PNF3C 上用隔离目录中的测试守护进程测试：

- 系统允许创建 MediaSession（`ownerUid=0`，`package=android`，`active=true`），SystemUI 收到通知并把它归为媒体类（`_media`）。
- 用户在控制中心看到了卡片，依次点了播放、暂停、下一首、上一首、真实麦克风和静音。每个回调都按顺序收到，对应命令全部成功；测试守护进程最后的状态是 `mode 2`（静音），和最后一次操作一致。拖动进度（`seek`）这次没有收到事件，HyperOS 卡片上有没有可拖动的进度条还要再确认。
- 停止时探针移除了通知并注销了会话，活动通知列表里已经没有它，只在通知历史（`mArchive`）里留有记录；正式服务 PID 3357 始终不变。

测试使用 `android` 包现有的 `DEVELOPER` 渠道，没有新建渠道。当时留下的问题（专用渠道、应用名、开机启动、WebUI 开关、进度拖动）已在 0.1.0.8 中处理，见下节。

## 正式功能：控制中心卡片（0.1.0.8，2026-10-18）

### 组成

- `bin/glassmic-media.jar`：`scripts/build.ps1` 把 `overlay/*.java` 编译成 DEX（做法同 TTS 工具）。卡片进程为 `app_process /system/bin --nice-name=glassmic-media io.mo.glassmic.overlay.MediaCard`，启动时沿用 `Main.systemContext()` 的步骤（ApplicationSharedMemory、`initializeMainlineModules`、以外部客户端获取 Settings 提供者）。`Main.initFonts` 保留在浮窗原型中；卡片不在进程内绘制文字，不需要调用。
- `bin/media-card.sh`：`status`、`on`、`off` 给 WebUI 用，输出一行 JSON；`supervise` 由 `service.sh` 在后台启动；`uninstall` 删除通知渠道。
- WebUI「设置 → 控制中心卡片」开关（中英文在 `webroot/i18n.js`）。开关写入 `/data/adb/glassmic/media-card.conf`（`enabled=1` 或 `enabled=0`），先写 `.tmp` 再 `mv` 原子替换。默认关闭。

### 启停与监督

- 监督进程只运行一份（以 `media-card-supervisor.pid` 判断），等 `sys.boot_completed` 后再开始，开关打开时启动卡片并 `wait`。卡片 PID 写入 `media-card.pid`；判断“仍在运行”时要求 `/proc/<pid>/cmdline` 为 `glassmic-media`（ART 会把 `comm` 改成主线程名 `main`，不能用），因此只会向自己的进程发信号。
- 关闭开关：先写设置，再创建 `media-card.stop`。卡片每秒检查一次，看到后移除通知、注销会话并以 0 退出。监督进程每轮先删除旧的停止文件、再读开关，两者顺序保证停止请求不会丢。若 5 秒内未退出，`off` 才结束这个已核实身份的 PID，并移除残留通知。
- 卡片非 0 退出（崩溃或被杀）时，监督进程移除残留通知，5 秒后重启；连续 5 次在启动后 60 秒内异常退出时，改为 5 分钟后再试。开关关闭会重置计数。
- 模块目录出现 `disable` 或 `remove` 标记时，卡片退出；监督进程结束，遇到 `remove` 时删除通知渠道。卡片日志在 `/data/adb/glassmic/media-card.log`，每次开机清空。
- 屏幕亮着时每秒读取一次状态，熄屏时每 15 秒一次。进度条由系统按 PlaybackState 推算，只有状态改变或位置偏差超过 0.75 秒时才更新会话，减少跨进程调用。守护进程不可用时卡片显示“音频服务未连接”，不提供操作。

### 通知渠道归属（实机发现）

`android` 包的通知按它的 UID 1000 记账，而通知服务按调用者 UID 保存渠道。root 进程直接调用 `createNotificationChannel` 会把渠道建在 UID 0 名下，`notify` 随后在 UID 1000 下找不到渠道，通知被丢弃（日志 `No Channel found for pkg=android, channelId=glassmic.media … notificationUid=1000`）。系统上下文自己报告的 UID 也是 0。

处理方式：

- 创建：通过 `INotificationManager.createNotificationChannelsForPackage("android", 1000, …)`，该接口允许系统调用者（含 root）。名称和说明随卡片语言刷新，用户改过的设置保持不变。
- 删除：`deleteNotificationChannel` 只作用于调用者自己的 UID，没有按 UID 指定的版本。`--uninstall` 因此要求以 UID 1000 运行：`media-card.sh` 把 DEX 复制到 `/data/local/tmp`（UID 1000 可读）后用 `su 1000 -c` 执行，再删除副本。KernelSU 的 `su` 支持指定 UID，已在测试机验证。
- 系统对删除的渠道只做标记（`mDeleted=true` 及删除时间），不会立即清除记录；渠道不再出现在设置里，也不能再发通知。同一 ID 以后重新创建时会恢复。

### 卸载

KernelSU 在下次开机的 post-fs-data 阶段运行 `uninstall.sh`，此时 system_server 还不能处理通知请求。因此分两路：卸载标记出现后，运行中的监督进程立即移除通知并删除渠道；`uninstall.sh` 在曾经使用过开关（存在 `media-card.conf`）时，把 DEX 复制到 `/data/local/tmp`，在后台等到开机完成（最长 10 分钟），再以 UID 1000 删除渠道并删除副本。两路都可重复执行。通知本身不会跨重启保留。`media-card.conf` 与其他设置一样在卸载后保留。`uninstall.sh` 的开机路径与监督进程用的是同一条命令，但完整卸载流程未在实机上执行。

### 应用名与进度拖动

- 应用名：通知附带 `android.substName`（`Notification.EXTRA_SUBSTITUTE_APP_NAME`，系统 API）。该附加信息需要 `SUBSTITUTE_NOTIFICATION_APP_NAME` 权限，`android` 包持有，通知服务保留了它（`android.substName=String (璃麦)`）。用户确认 HyperOS 控制中心卡片显示为“璃麦”，不再是 Android。
- 进度拖动：选中音频时 PlaybackState 声明 `ACTION_SEEK_TO`。用户在 HyperOS 控制中心卡片上拖动进度条，卡片进程依次收到 `seek 1609`、`seek 948`、`seek 1698`、`seek 1011`，对应命令全部成功。上一轮原型测试没有收到 seek，原因未确定；本轮是在播放中、时长 3 秒的循环音频上拖动的。

### 设备测试（2026-10-18，25019PNF3C，经用户同意）

`scripts/check_media_card.ps1` 把构建出的 `bin/` 文件与测试专用的控制器探针（`scripts/probes/MediaControl.java`，不打包进模块）推到 `/data/local/tmp/glassmic-dev-20261013/media-card`，由 `scripts/media_card_test.sh` 在该目录内启动私有测试守护进程和监督进程（`GLASSMIC_DATA` 指向私有数据目录，模块目录也是测试目录的副本）。正式模块、正式服务和 `/data/adb/glassmic` 未改动，正式 `glassmicd` PID 3357 前后不变。

`-Action auto` 全部通过：开关关闭时监督进程空转；打开后卡片运行、通知发出、UID 1000 下的渠道重要性为 2；会话声明可拖动；控制器经媒体会话服务发送的 seek、播放、暂停、下一首、静音、真实麦克风均到达守护进程；关闭开关后卡片经停止文件退出、通知移除；再次打开、`kill -9` 后监督进程重启卡片并重新发出通知；创建 `remove` 标记后卡片和监督进程退出、通知移除、渠道被标为已删除。`-Action manual` 让卡片留在手机上，由用户确认拖动和应用名，之后用 `-Action remove` 清理。

开发过程中的早期测试曾在 UID 0 名下创建了同名渠道，已用 root 删除；系统中仍留有这条已删除记录（`android` 包 UID 0），不可见、不影响功能。

### 仍未验证

- 正式安装 0.1.0.8 后的开机自动启动，以及从 KernelSU 管理器里的 WebUI 操作开关（测试用的是同一脚本，但未经过管理器的 `exec`）。
- 实际卸载模块后由 `uninstall.sh` 删除渠道的开机路径。
- 锁屏上的卡片、其他 ROM 的媒体卡片样式。卡片点击没有跳转目标：无 APK 时没有可以安全打开的 Activity。
