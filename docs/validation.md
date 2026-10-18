# 验证记录
## 0.1.0.9 生效应用显示应用名 · 2026-10-18（Asia/Shanghai，未安装）
- 构建：`scripts/build.ps1` 两次结果一致，16 KiB 对齐检查通过。
- 接口：测试机上的 KernelSU 管理器 v3.3.1（32680）的 DEX 中有 `getPackagesInfo`/`listPackages`。上游源码显示，管理器打开模块 WebUI 前会加载应用列表（`WebViewHelper.kt`），`getPackagesInfo` 从中返回 `appLabel`；未收录的包返回 error，页面改为显示包名。
- WebUI：PC 浏览器模拟桥接，应用名与包名副标题显示正确；无应用名的包回退为包名；已勾选的应用排在前面；勾选后列表不立即跳动，保存后重新排序；按应用名搜索可用。
- 未验证：手机 KernelSU 管理器内的实际显示。

## 0.1.0.8 控制中心卡片 · 2026-10-18（Asia/Shanghai，未安装）
- 构建：`scripts/build.ps1` 通过；新增的 16 KiB 检查确认 5 个 ELF（glassmicd、glassmicctl、Zygisk 加载器、后端、`libshadowhook_nothing.so`）的 LOAD 对齐均为 0x4000。两次构建 ZIP 一致。
- WebUI：PC 浏览器模拟 KernelSU 桥接，开关在中文/英文下开启、关闭，调用 `media-card.sh status/on/off`，状态与提示文字正确。
- 主测试机 25019PNF3C：经用户同意，用 `scripts/check_media_card.ps1` 在隔离目录测试，`-Action auto` 全部通过；用户在控制中心卡片上拖动进度条（4 次 seek 均成功），确认应用名显示为“璃麦”。正式 glassmicd PID 3357 不变，测试结束后无残留进程和临时文件。细节见 [悬浮控制方案](floating-controls.md)。
- 未验证：正式安装后的开机自启、管理器内 WebUI 的实际操作、卸载后的开机删除路径。

## 0.1.0.6 安装与重启核对 · 2026-10-15（Asia/Shanghai）

经用户许可，在 25019PNF3C 上用 `ksud module install` 安装 `dist/glassmic-0.1.0.6-arm64.zip`（退出码 0），然后重启一次。安装包在本地、记录和手机三处的 SHA-256 一致：7fd149ec…7da2。

- **重启前**：`modules_update/glassmic` 中 23 个文件与 ZIP 逐一一致，只缺 `customize.sh`（KernelSU 执行后删除）。
- **重启后**：boot_id 已变化，系统仍为 Enforcing，`modules_update` 已清空。`ksud module list` 显示 glassmic 0.1.0.6 / 8 已启用；同时待生效的 susfs4ksu_unified v0.1.1 也随这次重启生效。`glassmicd` 由 3298 变为 3333，状态报告版本 0.1.0.6。
- **已安装文件**：21 个文件与 ZIP 一致，包括全部原生程序、后端、加载器、辅助库、WebUI 和 sepolicy.rule。两处差异都在预期内：`customize.sh` 已被删除；`targets.txt` 由守护进程从用户保存的目标列表同步而来（录音机、微信），标签为 `system_file`。
- **真实 Zygisk 路径**：微信在开机后自行启动，本次未启动或重启任何应用。PID 10119 的状态为 hooks 15、stage 3、hookError 0、metadataReady 1，证明加载器打开辅助库并调用 `glassmic_prepare` 的新路径在真实应用中可用。微信的内存映射中只有新版后端、共享 PCM / 统计文件和 ShadowHook 入口页，辅助库在扫描后已卸载。
- **旧版 LSPosed GlassMic**（io.mo.glassmic）：开机后只作用于 Telegram，Telegram 不在目标列表中，没有作用于微信。

本次没有进行实际录音（reads 为 0），长时间录音稳定性仍待验证。推送到 `/data/local/tmp` 的安装包已删除。证据见 `device-evidence/post-reboot-status-0.1.0.6.json`。
## 0.1.0.6 主测试机 root 隔离测试 · 2026-10-15（Asia/Shanghai，未安装）

设备为 25019PNF3C / xuanyuan（无线 ADB 192.168.31.188:13555），SDK 37，构建号 CP2A.260605.016，KernelSU（`u:r:ksu:s0`），SELinux Enforcing。已安装模块仍为 0.1.0.4，其 `glassmicd` 的 PID 在测试前后都是 3298，测试没有影响它。`modules_update` 中另一个模块 susfs4ksu_unified 的待生效更新未被触碰。

ROM 核实：`/system/lib64/libshadowhook_nothing.so` 存在；搜狗输入法和游戏中心也各自附带一份。`/system/lib64` 在默认命名空间的搜索路径中，这解释了 0.1.0.4 为什么在这台机器上能装上 hook、在 Mi 9T 上不能。zygote64 的内存映射中没有任何 shadowhook 库，因此按描述符加载模块自带的副本时，不会被同 soname 的已加载库挡住。

`device_check.py` 全部阶段通过，退出码 0：
- selftest：读取池测试、`embedded ShadowHook initializes`、`AAudio and OpenSL hooks install in test process` 全部通过。
- SELinux 探针：zygote 域中调用 `glassmic_prepare`，再切换到 UID 10123 和 untrusted_app 域，结果 hooks 11、stage 3、error 0，`SELINUX PROBE PASSED`。
- 集成测试：RMS 0.194191 / 0.193999，与 Mi 9T 一致。
- 测试进程没有产生 AVC 拒绝。

证据见 `device-evidence/192.168.31.188_13555/`。

仍未验证：0.1.0.5 / 0.1.0.6 都未安装，Zygisk 加载器的新路径（打开辅助库并调用 `glassmic_prepare`）和真实应用取音，需要安装并重启后，在选中应用中确认。
## 0.1.0.6 ShadowHook 自带辅助库 · 2026-10-15（Asia/Shanghai，未安装）

问题：见下一节的 Mi 9T 测试。ShadowHook 在 hook 完 linker 后会 `dlopen("libshadowhook_nothing.so")`，借这个库的构造函数调用扫描 soinfo 布局。模块此前没有附带这个库，只有 ROM 默认库路径里恰好有同名库时，hook 才能装上。

修复：
- `native/CMakeLists.txt` 编译上游 `nothing/sh_nothing.c`，打包到 `lib/nothing/arm64-v8a/libshadowhook_nothing.so`。ShadowHook 按路径结尾匹配文件名，所以文件名必须保持不变。
- 后端链接 `-Wl,--wrap=dlopen`，`__wrap_dlopen` 只拦截这一个名字，用 `android_dlopen_ext` 加 `ANDROID_DLEXT_USE_LIBRARY_FD` 加载模块附带的库；其他调用和没有描述符的情况都交给原 dlopen。第三方源码未修改。
- Zygisk 加载器在 preAppSpecialize 阶段打开该文件，此时仍是 Zygote 域，与加载后端的方式相同。加载器调用新导出的 `glassmic_prepare(fd)` 初始化 ShadowHook，安装 hook 仍在 postAppSpecialize 线程中进行。`glassmic_hook_check` 改为 `glassmic_prepare`。
- 反汇编确认 `sh_linker_init` 调用的是 `__wrap_dlopen`；`bytesig_ctor` 的 dlopen 原样透传。TLS 检查通过，新库 LOAD 对齐为 0x4000。

Mi 9T（b7c9facb，Magisk，Enforcing）隔离测试：`device_check.py` 全部阶段通过。selftest 中 `embedded ShadowHook initializes` 和 `AAudio and OpenSL hooks install in test process` 均通过。SELinux 探针按真实流程在 zygote 域调用 `glassmic_prepare`，然后切换到 UID 10123 和 `untrusted_app` 域安装 hook，结果为 hooks 11、stage 3、error 0，`SELINUX PROBE PASSED`，测试进程没有产生 AVC 拒绝。集成测试 RMS 0.194191 / 0.193999。测试副本的辅助库标记为 `system_lib_file`，模拟 Zygote 映射模块文件。证据见 `device-evidence/b7c9facb/`。

| 文件 | SHA-256 |
|---|---|
| dist/glassmic-0.1.0.6-arm64.zip | 7fd149ec4eb3518fa4d9776b3686b5a9027ff0840a6134a1b68f17fed6237da2 |
| lib/arm64-v8a.so | b8a2ef5df53fbd16f06dc2b00f89c7a034d92f996782afd7a1ce91c2c0f43174 |
| zygisk/arm64-v8a.so | 3aa2a2aa0c04547233f8d50677447c3284b9b1f5609589de1db0f77c19fc1d17 |
| lib/nothing/arm64-v8a/libshadowhook_nothing.so | 5fb195ee383de78f5439944f8f093b3bb382f8358a51573e31d99ed5a671a706 |
| bin/glassmicd | 55dee88ddd9b6bbe281c7f929ced93717117bdb8c06c12d0457ee4d50185741b |

ZIP 中 24 个条目与源文件或构建产物逐一比对一致。打包只重新链接了 `glassmicd`（版本号字符串），三个 `.so` 与设备上测试的完全相同。

未验证：Zygisk 加载器的改动（打开辅助库并调用 `glassmic_prepare`）只能在实际安装后，由 Zygisk 加载到选中应用时验证；selftest 不经过这条路径。主测试机 25019PNF3C 上是否自带同名库、本修复在该机上的表现，也都需要复验。
## 0.1.0.5 Mi 9T root 隔离测试 · 2026-10-15（Asia/Shanghai）

设备为 Mi 9T（b7c9facb / davinci），LineageOS 15 userdebug，Magisk 30.7，SELinux Enforcing。Shell 的超级用户权限由用户在 Magisk 中授予。该机没有安装本模块，也不是 KernelSU，只用于验证原生程序。主测试机 25019PNF3C 的无线 ADB 处于 offline，未参与测试。测试只使用 `/data/local/tmp/glassmic-dev-20261013` 及其私有数据目录，测试守护进程结束后已退出。0.1.0.5 的后端和守护进程哈希与打包记录一致。

| 阶段 | 结果 |
|---|---|
| selftest | 新增 5 项读取池测试及原有 Reader、解码、媒体测试全部通过；`embedded ShadowHook initializes` 失败（init-errno 12） |
| 守护进程命令 | 10 条全部通过 |
| SELinux 探针 | FD 在 zygote 域接收、只读主 PCM 拒绝可写映射、UID 10123 加 `untrusted_app` 域均通过；hook 安装失败（stage 4、error 12） |
| 集成测试 | 全部通过：两路消费者 RMS 0.194191 / 0.193999，暂停、Seek、白名单移除、守护进程重启恢复、UID 降权后读取均通过 |

ShadowHook 失败的原因：带调试日志的探针显示，xdl 打开 linker64 成功，对 `soinfo::call_constructors` 和 `call_destructors` 的 hook 也成功；随后 `sh_linker.c` 调用 `dlopen("libshadowhook_nothing.so")` 扫描 soinfo 结构，这一步失败。`native/CMakeLists.txt` 没有编译 ShadowHook 的 `nothing/` 库，模块也没有附带这个库；Mi 9T 的系统目录中也没有。已交付的 0.1.0.4 后端在同一设备上同样失败，所以这是此前就存在的可移植性问题，与 TLS 修复无关。25019PNF3C 上此前初始化成功，推测该 ROM 的默认库路径里自带同名库，尚待在该机核实。

测试脚本的调整：`device_check.py` 现在支持 Magisk（把规则中的 `ksu` 换成 `magisk`，用 `magiskpolicy --live` 应用）。Magisk 下还额外加入一条仅供测试的 `allow zygote magisk unix_stream_socket`：SELinux 探针用 magisk 域创建的 socketpair 给 zygote 子进程传 FD，首次运行时 AVC 拒绝了 read。selftest 在没有 `untrusted_app_34` 的 Android 15 上改用 `untrusted_app` 域。某一阶段失败时，脚本会继续执行后续阶段，最后汇总失败项。证据改为按 serial 存放：`device-evidence/b7c9facb/`，其中 `shadowhook-debug.txt` 记录调试日志和根因。原 `device-evidence/` 下的 xuanyuan 0.1.0.3 证据未改动，另有副本在 `device-evidence/0.1.0.3/`。测试加入的 SELinux 规则在 Mi 9T 重启后失效。
## 0.1.0.5 打包 · 2026-10-15（Asia/Shanghai，未安装）

`scripts/build.ps1` 完整构建成功，构建时 TLS 检查通过。版本核对：本地无 tag 和远程仓库，`dist` 中没有 0.1.0.5；官方 Release 仅为原 APK 项目的 1.4.x，编号独立。ZIP 中 23 个条目逐一与源文件或构建产物比对一致，未混入暂存目录残留。脚本无 CR 换行，可执行权限正确，module.prop 为 0.1.0.5 / versionCode 7。两份 native 库 LOAD 对齐均为 0x4000，无本项目的 TLS 变量；后端导出 `glassmic_bind/install/metadata/hook_check`，加载器导出 Zygisk 入口；守护进程报告版本 0.1.0.5。版本号修改只重新链接了 `glassmicd`，ZIP 中的后端与下节 Mi 9T 上测试的二进制相同。

| 文件 | SHA-256 |
|---|---|
| dist/glassmic-0.1.0.5-arm64.zip | b4520ad75cd734b8d6d796494cdeed8f2d65e627edb68e6105b5403795e891ec |
| lib/arm64-v8a.so | 3158f9f088a2d9bf233678554614559a1695a464b1e7847074ff74283d1056c5 |
| zygisk/arm64-v8a.so | f76ea403a0bedcaf9cf56ed334392c88cac3ee3dde1b2dd2cd28df9836350725 |
| bin/glassmicd | 0342fe18f34f8f6d913dbfb0226a9c159440d4cc08b9eabff108fe0c34843c40 |

已交付的 0.1.0.4 ZIP 未改动，SHA-256 仍为 b89e2549…6c61。安装前仍需在主测试机用 root 运行 `scripts/device_check.py`，完成集成测试和 SELinux 探针。

## 音频回调 TLS 审计 · 2026-10-15（Asia/Shanghai）

问题：0.1.0.4 后端的 `fillAudio` 中 `static thread_local` 读取器数组（8 × 约 4.4 KB）和 `insideAAudio` 被 NDK r26（android-29）编译为 emutls。llvm-readelf 显示 `__emutls_v._ZZN2gm9fillAudio…readers` 和 `__emutls_v._ZN2gm12insideAAudioE`，没有 PT_TLS。compiler-rt `emutls.c` 显示：线程首次访问时 `emutls_get_address_array` 会 `malloc`/`realloc`，`emutls_allocate_object` 会 `malloc`/`posix_memalign`；进程首次访问还会经过 `pthread_once` 和 `pthread_mutex_lock`。AAudio/OpenSL 回调线程第一次取音即会命中该路径，违反回调中禁止堆分配和阻塞锁的规则。若改用 ELF TLS 也不能解决：bionic `tls_get_addr_slow_path` 在 dlopen 加载库的线程首次访问时同样会屏蔽信号、取 rwlock 写锁并调用 `memalign`。

修复：去掉后端所有 `thread_local`。读取器改为进程级 `ReaderPool<32>`（reader.h），AAudio 读取线程标记改为固定 `pthread_t` 原子表。新增构建检查。反汇编确认 `ReaderPool::fill` 仅调用 `Reader::fill`、`clock_gettime`、`memset` 和原子 swap，后端剩余 emutls 仅在 libc++abi 的 `__cxa_get_globals` 中，仅抛异常时使用。LOAD 对齐仍为 0x4000。

设备：主测试机 25019PNF3C 无线 ADB 未连接，因此仅在当时 USB 连接的 Mi 9T（Android 15，Enforcing）上用 shell 身份（无 root）在 `/data/local/tmp` 隔离目录运行 selftest，结束后删除该目录。新增 5 项读取池测试（同流连续、多流独立、满池放行、空闲回收、4 线程并发）和原有 Reader/解码/媒体测试全部通过。`embedded ShadowHook initializes` 失败，错误码为 init-errno 12（INIT_LINKER）；已交付的 0.1.0.4 后端在同一环境下同样失败，属于非 root 测试环境限制，与本修改无关。集成测试、SELinux 探针和真实应用取音需要 root，未运行，待在主测试机复验。证据保存在 `device-evidence/tls-pool-selftest-mi9t.txt`。
## 0.1.0.4 微信实机验证 · 2026-10-14（Asia/Shanghai）

第二次获准重启后，0.1.0.4 已生效，真实微信进程的 hook mask 为 15、stage 3、hookError 0、metadataReady 1。已安装的模块元数据、SELinux 规则、原生程序、Zygisk 加载器和 WebUI 与构建产物一致。

微信随后提示“语音被其他应用占用，或因系统错误，可尝试重启手机”。只读检查发现旧版 LSPosed GlassMic 1.4.6 仍启用并作用于微信，日志确认两版同时加载；系统录音权限允许，检查时没有活跃录音占用，但新版取音计数为 0。用户取消勾选旧版后反馈录音恢复；随后微信 PID 30577 的新版取音计数为 58 次、111,360 帧，证明新版已进入实际音频替换路径。该快照记录欠载 4,806 帧，未完成长期连续录音稳定性测试。

故障前后状态保存在 `device-evidence/voice-error/module-status.json` 和 `device-evidence/voice-error/after-old-module-disabled.json`，安装校验保存在 `device-evidence/post-reboot-status-0.1.0.4.json`。取消旧版由用户操作，未由测试脚本跳转界面或重启微信。此问题通过停止两版同时作用于微信解决，没有另发修复版本。

## 0.1.0.4 AudioRecord 字段兼容

首次获准重启后，0.1.0.3 已生效，微信 PID 12521 的 hook mask 为 11、stage 3、error 0，证明共享文件权限及 AAudio/OpenSL 初始化修复生效；Java AudioRecord metadataReady 为 0。

只读的 `scripts/check_audio_record_fields.ps1` 在实际 ROM 检查到原生字段已改名为 `mNativeAudioRecordHandle`，三个格式 getter 和 native_start(II)I/native_release()V 签名仍正确。[AOSP android_media_JNIUtils.h](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/core/jni/android_media_JNIUtils.h) 的 getFieldSp 确认新字段仍保存底层指针。修复只兼容新/旧字段名，未增加 C++ 对象扫描或录音回调中的 JNI 调用。检查程序不构造 AudioRecord，不打开麦克风。

0.1.0.4 的真实 Zygisk 元数据初始化与微信取音已核对，结果见本页第一节。

0.1.0.4 隔离 native smoke test 通过；更新已安装并按用户许可重启生效，module.prop、原生库、WebUI 和 sepolicy.rule 哈希与构建一致。ZIP SHA-256：`b89e2549f156b2ffad8f0c8308530f48ff32da2bc5889e138936db3f41ca6c61`。

## 0.1.0.3 设备隔离验证 · 2026-10-14（Asia/Shanghai）

使用用户新确认连接的 25019PNF3C / xuanyuan，固定指定无线 ADB；未使用另一台 Mi 9T。仅启动/停止本项目的测试进程，没有跳转手机界面、重启手机、现有应用或现有 Android 服务。测试期间新增了专用 `glassmic_shm` 类型的访问规则，系统保持 Enforcing，原模块服务 PID 3246 持续运行。

| 项目 | 证据/方法 | 结果 |
|---|---|---|
| Hook 0 原因 | 手机 auditd/AVC，旧共享文件 ls -lZ | Zygote 收到 `/dev/glassmic/audio.shm` 的 FD 时被拒绝 read，旧标签为 `u:object_r:device:s0` |
| SELinux 修复 | device-evidence/native-selinux.txt | KSU 传 FD → Zygote 接收及映射 → UID 10123/untrusted_app_34，AAudio/OpenSL hook mask 11、stage 3、error 0，并读取到非静音 PCM |
| 主 PCM 权限 | 同上及原生集成测试 | 只读 FD 无法映射为可写，普通应用只更新独立统计映射 |
| 播放时钟 | device-evidence/native-integration.txt | 未附加任何录音消费者时仍推进；暂停精确保持位置 |
| 双路供音 | 同上 | 48 kHz mono 与 16 kHz stereo RMS 0.194191/0.193999；Seek、白名单更新、测试服务恢复通过 |
| FLAC | device-evidence/import-regression.json | 16 位、24 位、192 kHz FLAC 可列出/导入/解码/Seek，复制后 SHA-256 一致 |
| 大文件直接导入 | 同上，19,201,965 B MP3 | 手机内部复制 + fsync 29 ms；包含一次 ADB 命令调用约 81.75 ms，不含事先把测试文件传到手机的时间 |
| 文件边界 | 同上 | 同名导入生成独立副本，原始文件保留，系统路径拒绝导入；测试结束只清理本轮测试文件 |
| 待重启安装 | device-evidence/staged-update.json | 0.1.0.3 / versionCode 5 安装到 modules_update；原生库、WebUI、SELinux 规则与构建产物哈希一致；现有服务仍为 0.1.0，未重启 |

最终安装包 SHA-256：`2dedc01bc2feb5222c8f98487a2e71c93da8e8e52e9820e9023c268d786276a0`。正式模块基准和已占用的本地修订号见 [发版记录](releases.md)。

上述为隔离测试。KernelSU 管理器的真实选择器、微信 Java AudioRecord 录音、应用进程重建和长期稳定性仍需更新生效后验证。

## 修复前本地验证 · 2026-10-14（Asia/Shanghai）

测试设备重新连接前，按用户指示仅做本地验证。下面的浏览器结果来自 PC Chromium 模拟 KernelSU 桥接，不能代替手机管理器的真实文件选择与界面操作。

| 项目 | 方法 | 结果 |
|---|---|---|
| 解码 | scripts/host_decoder_check.ps1，同一 native/decoder.cpp | 16 位/44.1 kHz FLAC、24 位/96 kHz FLAC、192 kHz FLAC、PCM16 WAV，以及 19,201,965 B 的 MP3，读取非静音 PCM 和中点 Seek 通过 |
| 播放进度 | 浏览器模拟服务时钟 | 播放时滑块前进；暂停后位置保持不变 |
| 服务断线 | scripts/check_webui_connection.js | 断线后进度冻结、播放禁用；恢复连接后控制恢复 |
| FLAC 选择 | 系统选择器类型过滤及模拟文件浏览页 | file input 不再设置 accept；大写 .FLAC、含单引号的文件名可选择 |
| 直接导入 | 模拟本地文件导入 | 跨目录勾选 3 个音频，每个文件仅调用一次 import；复制错误显示在弹窗内，可重试或取消 |
| 系统选择器备用路径 | 浏览器文件上传 | 220,228 B FLAC 和 19,201,965 B MP3 完整分块传输、提交，合计 238 次 upload 调用 |
| 移动端布局 | 390 px 视口、浅色/深色及 200% 字号 | 页面与弹窗无水平溢出；浏览器控制台无错误；浅色正文/次要文字对比度 15.53:1 / 6.14:1，深色 13.64:1 / 8.57:1 |
| 构建和安装包 | scripts/build.ps1、ZIP 检查、llvm-readelf | 0.1.0.3 ZIP 构建成功，包含当前 WebUI 和第三方许可证，无 APK；两份 native 库 LOAD 对齐均为 0x4000 |

设备重新连接后的原生回归与复制耗时结果见本页第一节。真实微信加载及管理器界面验证仍未执行。

## 0.1.0 历史设备验证 · 2026-10-13

## 已验证
| 项目 | 证据 | 结果 |
|---|---|---|
| 设备 | ADB getprop / root id | 小米 15 Ultra（xuanyuan），Android 17/API 37，Linux 6.6.118，SELinux Enforcing |
| 模块环境 | 读取 module.prop | KernelSU 32680、Zygisk Next 1.5.0 |
| 构建 | scripts/build.ps1 | C++、DEX JAR 和模块 ZIP 构建成功，无控制 APK |
| 原生基础 | device-evidence/native-selftest.txt | PCM16/Float、重采样、声道转换、欠载计数、静音、真实输入与心跳故障回退通过 |
| 后端加载 | 同一自测进程 | 独立 native 后端可 dlopen；嵌入 ShadowHook 初始化及 AAudio/OpenSL Hook 安装通过 |
| 双消费者 | device-evidence/native-integration.txt | 48 kHz 单声道与 16 kHz 双声道同时读取；RMS 0.194191 / 0.193999 |
| 启动欠载 | 同上 | 961 / 322 帧，约 20 ms；后续连续供音通过 |
| 控制操作 | 同上 | 暂停清旧音频、Seek、移除白名单立即放行通过 |
| 服务恢复 | 同上 | 仅重启隔离测试服务；选曲保留，默认真实麦克风，既有映射恢复后继续供音 |
| UID 降权 | 同上 | 消费者无 root FD，普通 UID 下已有映射可用 |
| 系统 TTS | 模块 headless 工具 | 当前系统引擎 com.xiaomi.mibrain.speech 合成 WAV 成功；生成文件可被原生解码 |
| ELF | NDK llvm-readelf / llvm-nm | LOAD 对齐 0x4000；Zygisk 入口导出；主加载器没有 AAudio/OpenSL 依赖 |
| WebUI | PC Chromium 模拟 KernelSU 桥接 | 音频选择含单引号文件名、播放、循环；浅色/深色及 200% 字号无水平溢出 |

原生测试没有打开真实麦克风或播放扬声器，没有启动游戏、跳转手机界面、重启设备或重启既有 Android 服务。TTS 测试只把合成结果写成文件。

## 未完成的设备验证
- KernelSU 管理器内的真实文件选择器、WebUI 完整操作。
- 重启后 Zygisk 在目标应用中的真实加载。
- 真实 Java AudioRecord、AAudio read/callback/MMAP、OpenSL 录音输出对比。
- 长时间录音、热切换、应用进程重建与多应用并发压力测试。

以上依赖用户允许重启或界面跳转；目前不能宣称所有应用均兼容。
