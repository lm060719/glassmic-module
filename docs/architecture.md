# 实现说明
## 控制和供音
WebUI 调用 KernelSU 的官方 JS `exec` 接口运行 `glassmicctl`。工具把固定结构的命令发送到仅 root 可访问的 Unix socket，音频不经过 WebView、JSON 或 shell。

`glassmicd` 以 48 kHz 单声道 Float PCM 为主时钟，每 10 ms 生成一块音频。音源解码、增益、限幅、变速和播放状态由原生进程管理。0.1.0.3 起，播放时直接推进音源，暂停时保持位置，不再依赖消费者录音心跳；多个消费者共享播放时间线。后加入的消费者接收当前进度的声音，播放控制不负责手机扬声器试听。

共享环形缓冲为 1 秒，消费者正常从约 20 ms 的预缓冲位置读取。消费侧按目标采样率转换，降采样使用 17 tap、64 相位 FIR；每个录音流有独立游标，不会互相抢走数据。采样单元和状态使用无锁原子，音频回调中不进行 JNI、IPC、堆分配或阻塞锁等待。

后端不使用 `thread_local`。NDK 在 Android 上把它编译为模拟 TLS（emutls），线程首次访问时会调用 `malloc` 并可能加锁，回调线程正好命中这一路径。每个录音流的读取状态存放在进程级固定读取池中：以流指针为键，用非阻塞 try-lock 占用槽位，槽位被占用时改用其他槽位，不等待。AAudio 阻塞读取期间所在线程记录在固定 `pthread_t` 表中，避免内部 AudioRecord 重复取音。`scripts/build.ps1` 用 llvm-readelf 检查后端，发现本项目的 TLS 变量即构建失败。

## Zygisk 加载
主加载器不链接 AAudio/OpenSL，未选中的应用只读取白名单并卸载模块。仅选中的应用加载后端，连接 root companion，接收只读主音频 FD 及可写的消费者统计 FD；在 UID/SELinux specialization 之前建立映射并关闭 FD。

ShadowHook 也在 specialization 之前初始化。它 hook linker 后会 dlopen `libshadowhook_nothing.so`，借这个库的构造函数调用扫描 soinfo 布局。模块自带这个库（`lib/nothing/arm64-v8a/`），加载器从模块目录按描述符打开，交给后端的 `glassmic_prepare`。后端用 `--wrap=dlopen` 把这一次加载改为 `android_dlopen_ext` 按描述符加载，因此不依赖 ROM 自带该库。音频 hook 的安装仍在 postAppSpecialize 的线程中进行。

PCM 和统计文件使用专用 `glassmic_shm` SELinux 类型。模块的 `sepolicy.rule` 仅给 KSU、Zygote 和 appdomain 配置该类型的访问；不放宽通用 `device` 文件权限，不关闭 Enforcing。目录仍为 root 0700，文件为 0600，主 PCM 的 FD 为只读。收到文件描述符不等于允许读取文件：旧版 `device` 标签会在 Zygote 接收 FD 时被 SELinux 拒绝；因此 daemon 创建及恢复共享文件时必须设置正确标签，标签不可用时启动失败并写入明确日志。

后端覆盖 AAudio 阻塞读取和回调、OpenSL 缓冲队列，以及 Java AudioRecord 使用的 native read。AudioRecord 的格式在 `native_start` 控制操作时读取并缓存，不扫描私有 C++ 对象内存。未知格式/未识别对象放行真实输入。

系统进程、系统关键包和管理器被排除。主加载器跳过 `system_server`。白名单移除通过共享状态立即让已有后端放行；新增应用需要下次启动加载。

控制侧固定容量表限制资源增长：服务最多 128 个消费者连接；每个应用进程内，AAudio 128 个并发回调流/64 个构建器、AudioRecord 64 个活跃对象、OpenSL 64 个活跃录音对象、读取池 32 个同时使用的流。关闭对象或空闲流会回收槽位，超出容量时放行原始录音。

## 故障恢复
服务心跳超过 500 ms 未更新、白名单移除、协议不匹配或后端不可用时，录音端放行真实麦克风。初始化连接有短超时，避免应用启动长时间等待。

运行时共享文件位于 `/dev/glassmic` 的 tmpfs；服务重启复用同一 inode，恢复仍存活的消费者。新服务先切到真实输入并更新 generation。用户恢复播放后，已有映射可继续使用。音源切换、暂停和 Seek 都更新 generation/floor，防止播放旧缓冲。

设置、白名单、选曲使用临时文件加原子 rename 保存。位置在控制操作时保存，不按音频帧写磁盘。服务脚本只监督自己的原生进程，不重启 Zygote 或 audioserver。

## 文件与 TTS
默认使用模块内的文件浏览页，仅浏览共享内部存储。工具解析真实路径、检查普通文件及大小，直接复制到私有临时文件，fsync 后以 hard link 原子发布；同名自动生成新名字，失败清理临时副本，保留原始音频。每个文件只调用一次 Root 命令。

系统文件选择器作为备用入口，不设置 MIME/accept 过滤，选择后按扩展名检查 WAV、MP3、FLAC。该入口按 80 KiB 分块传输，以固定参数传给工具；工具检查偏移和文件名，写入 `.part`，完成后 fsync + rename。两种入口均使用 POSIX 单引号转义命令参数，文件名在页面通过 textContent 显示。

TTS 工具是小型 DEX JAR，由 `app_process` 运行，通过 Android 的 `texttospeech` 会话服务连接已安装引擎。该路径不需要应用注册、Activity 或控制 APK。先在 shell 临时目录生成 WAV，再由 root 移入模块音频库，避免把模块私有文件 FD 直接交给语音引擎。

## 尚待验证
隔离测试已验证原生数据面与当前 ROM 的系统 TTS。真实应用中的 Zygisk 注入、Java AudioRecord 元数据注册、AAudio MMAP 实际录音和 OpenSL 对象生命周期需要获准后进行设备交互测试。现有测试没有对游戏账号或反作弊结果做任何验证。
