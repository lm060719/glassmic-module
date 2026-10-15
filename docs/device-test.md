# 设备回归测试流程

供测试代理（如 Gemini 通过 `agy`）或人工在 root 主测试机上执行。目标：用已构建好的 `build/native/` 原生程序，在隔离目录完成 selftest、守护进程命令、SELinux 应用身份探针和集成测试，并把结果写成报告。

## 前提
- 工作目录为项目根目录。原生程序已由 `scripts/build.ps1` 构建，**不要重新构建**。
- 测试设备由启动提示指定，**只对指定的那一台执行命令**，另一台不要碰：

| 设备 | serial / 连接 | model / device | root |
|---|---|---|---|
| 主测试机 | 无线 ADB，上次地址 `192.168.31.188:13555` | 25019PNF3C / xuanyuan | KernelSU，装有 GlassMic 模块 |
| Mi 9T | USB，`b7c9facb` | Mi 9T / davinci | Magisk，Android 15 LineageOS；不是本模块的目标环境，只验证原生程序 |

- adb 若不在 PATH 中，使用 `E:/platform-tools/adb.exe`；python 使用 PATH 中的 python。

## 步骤
1. **找设备**：执行 `adb devices -l`，按上表找到指定设备，记下它的 serial，下文记为 SERIAL。
   - Mi 9T 应已通过 USB 连接，找不到就停止。
   - 主测试机找不到时，执行 `adb connect 192.168.31.188:13555` 和 `adb mdns services`，用 mdns 列出的地址 `adb connect`。若仍未出现，每 30 秒执行一次 `adb devices -l`，最长等 10 分钟（用户可能正在重新开启无线调试）。超时就停止，在报告中写明“设备不可达”。不要扫描网段，不要执行 `adb pair`。
2. **只读核对**（以下命令都加 `-s SERIAL`）：
   - `adb -s SERIAL shell getprop ro.product.model` 和 `adb -s SERIAL shell getprop ro.product.device`，与上表不符就停止。
   - `adb -s SERIAL shell "su -c id"`，必须是 uid=0，否则停止。Magisk 设备若返回 Permission denied，说明 Shell 未获超级用户授权，停止并报告，不要尝试其他提权方式。
   - 主测试机：`adb -s SERIAL shell "su -c 'cat /data/adb/modules/glassmic/module.prop'"`，记录已安装的版本。Mi 9T 没有安装本模块，跳过这一项。
   - `adb -s SERIAL shell "su -c 'pidof glassmicd'"`，记录已有服务的 PID（Mi 9T 应为空）。
3. **隔离测试**：在 PowerShell 中执行 `$env:GLASSMIC_SERIAL='SERIAL'; python scripts/device_check.py`（bash 中为 `GLASSMIC_SERIAL=SERIAL python scripts/device_check.py`）。脚本只使用 `/data/local/tmp/glassmic-dev-20261013` 和私有 `--data` 目录，启动和停止的是它自己的测试守护进程。脚本会给运行中的 SELinux 策略加上模块自带的 `glassmic_shm` 规则，这在预期之内：KernelSU 设备用 `ksud sepolicy apply`；Magisk 设备把规则中的 `ksu` 域换成 `magisk`，并加一条仅供 SELinux 探针使用的 `allow zygote magisk unix_stream_socket`，然后用 `magiskpolicy --live --apply` 应用，重启后失效。某一阶段失败时脚本会继续执行后续阶段，最后汇总失败项并以非零状态退出。证据写入 `device-evidence/<serial>/`。完整保留脚本的标准输出和错误输出。
4. **收尾核对**（无论第 3 步成功与否都执行）：
   - `adb -s SERIAL shell "su -c 'logcat -d -t 600'"`，只摘取含 `GlassMic`、`glassmic`、`shadowhook` 或 `avc` 的行。不要清空 logcat。
   - `adb -s SERIAL shell "su -c 'pidof glassmicd; ls /data/local/tmp/glassmic-dev-20261013'"`。确认已有服务的 PID 与第 2 步一致，测试守护进程也已退出。
5. **写报告**：把结果写入 `build/device-test-report.md`，格式见下文。这是唯一允许新建或修改的文件（`device_check.py` 自动写入的 `device-evidence/` 文件除外）。

## 禁止
- 修改仓库文件、尝试修 bug、运行构建（build.ps1、cmake、ninja、package.py）、执行 git 写操作。
- 安装、更新或删除模块；写入 `/data/adb/modules`、`/data/adb/glassmic`、`/dev/glassmic`。
- reboot、shutdown、setenforce、启动或停止系统服务。
- `am`、`pm`、`cmd`、`monkey`、`input`、`settings`、`wm` 等启动、停止应用或操作界面的命令；kill 不是本测试在 `/data/local/tmp` 启动的进程。
- 给真实应用装 hook，或把真实应用加入目标列表。
- 对未被指定的那台设备执行任何命令。
- 修改 Magisk 或 KernelSU 的授权、设置或数据库。
- 遇到失败时自行换方法或变通。失败了就停止，并如实写进报告。

## 报告格式（build/device-test-report.md）
```
# 设备回归测试报告
时间：
设备：SERIAL / 型号 / device / root 方案（KernelSU 或 Magisk）/ 已安装模块版本（无则写“无”）/ 已有 glassmicd PID（测试前、测试后）
## 命令记录
（每一步的命令和关键输出）
## device_check.py 输出
（原样粘贴完整输出，包括所有 PASS/FAIL 行）
## 关键项
- pooled reader keeps its stream position:
- second stream gets an independent reader:
- full reader pool falls back to real microphone:
- idle reader slot is reclaimed:
- concurrent callbacks share the reader pool:
- embedded ShadowHook initializes:
- AAudio and OpenSL hooks install in test process:
- SELINUX PROBE PASSED:
- INTEGRATION TESTS PASSED:
## 写入的 device-evidence/<serial>/ 文件
## 相关日志
## 结论
通过 / 失败 / 未能运行，并说明原因
```
