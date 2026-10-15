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

这条路线使用 Android 系统接口，需要针对 ROM 验证，不能仅凭 Root 权限保证兼容所有设备。当前还没有完整浮窗实现或新模块版本。

## 小米 15 Ultra / Android 17 检查结果

2026-10-14，仅使用授权的 25019PNF3C，无线 ADB 明确指定设备，运行本项目独立检查进程；没有显示窗口、跳转应用、打开麦克风或重启现有进程。

`scripts/probes/OverlayEnvironment.java` 的最终检查通过：UID 0；WindowManager 可取得；INTERNAL_SYSTEM_WINDOW 权限允许（返回 0）；普通 View 离屏绘制成功；windowDisplayed 为 false。原生音频服务 PID 3298 保持运行。

Android 17 的 WindowManager 在独立进程中要求初始化 ApplicationSharedMemory。检查使用进程内的新实例；没有改动系统进程或系统的共享内存。之前缺少初始化时，检查进程失败，加入初始化后通过。

标准 TextView 的首次文字绘制触发了本 ROM 的原生字体初始化断言。最终探针改为普通 View，以隔离验证权限与基础绘制；文字渲染问题仍需适配。跨应用真实窗口添加、拖拽和点击、字体缩放、横竖屏、锁屏隐藏、内存占用及进程退出也尚未验证。

只有完成上述兼容性处理，才将浮窗功能纳入模块。实际显示测试窗口前，依照用户约定取得对手机界面操作的许可。
