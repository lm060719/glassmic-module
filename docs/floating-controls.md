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

这条路线使用 Android 系统接口，需要针对 ROM 验证，不能仅凭 Root 权限保证兼容所有设备。原型见下文，尚未发布新的模块版本。

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

测试使用 `android` 包现有的 `DEVELOPER` 渠道，没有新建渠道。正式功能需要一个专用渠道，它会持久保存在系统的通知设置里，卸载模块时应一并删除。尚未处理的问题：卡片的应用名显示为系统（Android）；通知在开机后由谁启动；与 WebUI 的开关如何联动。
