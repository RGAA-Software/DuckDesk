# USBMMIDD 虚拟显示器实施与测试记录

日期：2026-08-22

源码：`D:\source\GoCloud\GammaRayPremium`

RustDesk 参考：`D:\source\rustdesk`
测试机：`10.0.0.90` / `WIN-RASS8RC6V3H`

> 仓库不保存测试机密码或可复用凭据。远程凭据仅用于本次 WinRM 会话。

## 1. 环境与基线

- 系统：Windows Server 2022 Datacenter x64，build 20348。
- GPU：NVIDIA GeForce RTX 4090，驱动 `32.0.15.9579`。
- 活动控制台：Administrator，Session 1。
- 既有显示驱动：Parsec `ROOT\\DISPLAY\\0000`、Oray `0001`、GameViewer `0002`、
  ToDesk `0003`、NVIDIA 物理显卡，以及 RDP Remote Display Adapter。
- 测试前不存在 USBMMIDD 设备或活动 USBMMIDD 显示器。

## 2. 驱动来源与完整性

- 来源：RustDesk 官方工作流使用的 `usbmmidd_v2.zip`。
- 归档 SHA-256：
  `629B51E9944762BAE73948171C65D09A79595CF4C771A82EBC003FBBA5B24F51`。
- 完整来源、许可证及逐文件哈希见 `third_party/usbmmidd_v2/`。
- `usbmmidd.cat` 和 x64/Win32 驱动 DLL 均由
  Microsoft Windows Hardware Compatibility Publisher 有效签名。
- 90 实机安装结果：Amyuni `USB Mobile Monitor Virtual Display` `2.0.0.1`，
  `IsSigned=true`，签名者为 Microsoft Windows Hardware Compatibility Publisher。

## 3. 功能结果

| 项目 | 结果 |
| --- | --- |
| 初次按需安装 | 通过；安装器返回码 1 但设备接口及签名驱动已成功出现，代码以接口出现作为权威结果 |
| 创建第一块 | 通过，逻辑 ID `usbmmidd-slot-1`，实机 `DISPLAY44` |
| 创建第二块 | 通过，逻辑 ID `usbmmidd-slot-2`，实机 `DISPLAY45` |
| 模式 | 两块均由交互会话枚举为 1920x1080@60Hz |
| 第三块 | 正确拒绝，进程返回 2，owned 数保持 2 |
| Service/进程重启对账 | 通过；每次 CLI 均为新进程，持久状态与实际数量一致 |
| LIFO 删除 | 通过；按 slot-2、slot-1 顺序删除 |
| 空集合继续删除 | 正确拒绝，进程返回 2 |
| DDA 热重建 | 通过；修复停止线程与 DXGI 资源释放竞态、清除旧 monitor 缓存；真实 WebClient 中稳定完成 1→2→1 |
| GDI 热重建 | 通过；500ms 防抖后从 3 个采集屏稳定变为 2 个 |
| Session 0 桥接 | 通过；SYSTEM/Session 0 父进程成功在活动控制台启动一次性 worker，完成创建与删除 |
| WebClient 悬浮入口 | 通过真实鼠标事件展开悬浮菜单并点击 `+`/`-`；一级面板显示“Virtual display (n/2)”，支持上限/空集合禁用、请求中防重复点击和错误提示 |
| WebClient 新屏采集/切换 | 通过；新增 `DISPLAY44` 后可采集独立虚拟桌面，可切回 `DISPLAY1`，两侧均有持续解码帧和截图证据 |
| WebClient 移除恢复 | 通过；点击减号后 2→1，自动重连，恢复采集 `DISPLAY1`，无黑屏、无进程崩溃 |
| 清理 | 通过；owned=0、活动虚拟屏=0、只剩物理 `DISPLAY1`；隔离目录/进程/端口/三个测试任务均清零 |
| 第三方显示驱动保护 | 通过；Parsec/Oray/GameViewer/ToDesk/NVIDIA 的设备 ID、版本和签名状态均未改变 |

最终持久状态：`desired_count=0`、`owned_slots=[]`、
`topology_generation=8`、`removal_safe=true`、`last_error=null`。
该阶段是功能开发测试，驱动设备当时保留安装但没有活动虚拟屏；后续安装包验收已覆盖完整驱动卸载，见第 6 节。

## 4. 真实 WebClient 全链路验收

最终验收不是直接调用 Service CLI，也不是只检查协议回包，而是在 Chrome CDP 中打开
`http://10.0.0.90:32392/web_client/?deviceId=vd-e2e-90`，使用真实鼠标事件操作悬浮球一级菜单的加减按钮。
远端每块屏放置独立动态色块窗口，确保解码帧增长代表所选屏幕有新的桌面像素到达 WebClient。

| 阶段 | 拓扑 / 当前采集 | 2.5 秒解码帧增量 | 结果 |
| --- | --- | ---: | --- |
| 物理屏基线 | `DISPLAY1` / `DISPLAY1` | +29 | 通过 |
| 点击 `+` 后 | `DISPLAY1, DISPLAY44` / `DISPLAY1` | +29 | 通过 |
| 切换虚拟屏 | `DISPLAY1, DISPLAY44` / `DISPLAY44` | +25 | 通过 |
| 切回物理屏 | `DISPLAY1, DISPLAY44` / `DISPLAY1` | +30 | 通过 |
| 点击 `-` 后 | `DISPLAY1` / `DISPLAY1` | +27 | 通过 |

机器可读证据：`tests/artifacts/virtual_display_e2e_90/result.json`（`result=PASS`）。

悬浮按钮证据：`tests/artifacts/virtual_display_e2e_90/01b_floating_virtual_display_controls.png`。
五阶段画面：同目录下 `01_baseline_physical.png`、`02_after_add_physical.png`、
`03_virtual_monitor_capture.png`、`04_switched_back_physical.png`、
`05_after_remove_recovered.png`。其中第三张明确显示虚拟屏独立桌面和 `screen 1` 动态窗口。

测试完成后再次从交互 Session 1 枚举，只有物理 `DISPLAY1`。测试隔离 Service、Render、
动态窗口、计划任务和目录全部删除；原 `pixels_service.json` 与
`virtual_displays.json` 恢复后 SHA-256 均与测试前备份一致。

## 5. 本地验证

- `cargo test -p service_core -p px_service`：`px_service` 49/49 通过；
  `service_core` 59 通过、0 失败、1 个既有环境测试忽略。
- `cmake --build build_official --config RelWithDebInfo --target px_render px_client cap_dda cap_gdi frame_carrier net_rtc_local net_ws`：通过。
- `npm run build`（`web/px_web_client`）：TypeScript 检查及 Vite 生产构建通过。
- `scripts/collect_dist.py --build-dir build_official`：通过，产物包含完整
  `dist/usbmmidd_v2/`、WebClient 和最新 Rust release `px_service.exe`；后者与
  `rust_client/target/release/px_service.exe` SHA-256 一致。
- C++ 构建仅有仓库原有警告，无本功能编译错误。

## 6. 安装包静默安装/卸载验收

提交前最终安装包：`output/build_official/3.3.51/Pixels_3.3.51_Setup.exe`

大小：`198231329` bytes

SHA-256：`6C16B6321C1327DC6334EC28F7BDBA1A9DD8E46703A98CED71F439136FAEFF0E`

| 项目 | 90 实机结果 |
| --- | --- |
| 干净安装/卸载基线 | 3.3.50 阶段从程序、服务、进程、PnP 节点和 Amyuni Driver Store 均为 0 的状态完成全流程验证 |
| 3.3.51 `/S` 静默覆盖安装 | 返回 0；安装目录、3.3.51 卸载信息和 `Uninstall.exe` 均存在；即使面板未运行也会先停止服务和全部业务进程再覆盖 DLL |
| 驱动安装 | 始终只有 `ROOT\\DISPLAY\\0004` 一个节点，状态 `OK`；Driver Store 为 `oem36.inf`、2.0.0.1，Microsoft Windows Hardware Compatibility Publisher 签名有效 |
| 模块启动 | `px_service` 为 Automatic/Running；`px_panel`、`px_render`、`px_function`、`px_osinfo` 各 1 个进程；20369/20371/20375 均监听 |
| 覆盖安装幂等 | `/S` 返回 0，升级前后 USBMMIDD PnP 节点仍为 1，未重复创建设备，所有模块自动恢复 |
| 真实 WebClient | 3.3.51 的 20371 WebClient 全链路 PASS；从已有 1 块 owned 屏增加第二块、采集新屏、切回物理屏、删除新增屏并恢复到原状态均通过 |
| `/S` 卸载 | 3.3.50 同驱动卸载实现已验证：安装目录、32/64 位卸载注册表、服务、`px_panel_start`、相关进程和端口全部不存在 |
| 驱动完整卸载 | USBMMIDD PnP 节点为 0，Amyuni Driver Store 包为 0，`System32\\drivers\\UMDF\\usbmmIdd.dll` 不存在 |
| 第三方保护 | Parsec/Oray/GameViewer/ToDesk 四个既有显示适配器仍为原 InstanceId 且状态 `OK` |
| 测试清理 | 两个提交前验收任务和 `Windows\\Temp` 临时安装包/脚本均清零；产品保持安装，用户原有 1 块 owned 屏保持不变 |

提交前最终 WebClient 证据位于
`tests/artifacts/final_commit_virtual_display_e2e_90_retry/`，`result.json` 为
`PASS` 且连接密码已脱敏。五阶段实际解码帧增量依次为
`+29 / +25 / +22 / +36 / +75`；新增屏为 `DISPLAY45`，结束后精确恢复到
基线 `DISPLAY1 + DISPLAY44`、owned=1、`topology_generation=17`、
`removal_safe=true`、`last_error=null`。

此前用户观察到全黑页面的根因是 DDA 在 DXGI access loss/D3D 设备创建失败后，
显示拓扑重建可能连续失败并永久保留空 monitor 缓存。当前实现连续三次重建失败后自动降级 GDI，
并在 DDA/GDI 切换时显式停用旧采集插件；3.3.51 实机回归已覆盖新增屏、独立画面采集、
物理/虚拟屏切换和删除恢复。

安装器当前未做 Authenticode 签名（`NotSigned`）；不影响本次管理员静默安装测试，但发布前应走正式代码签名流程。

最终 90 状态：3.3.51 保持安装供人工复验；服务和五个模块正常，USBMMIDD 设备/驱动健康；
用户原先保留的 `usbmmidd-slot-1`（`DISPLAY44`，1920x1080@60Hz）仍在，未被提交前测试删除。

## 7. 尚需发布矩阵覆盖

90 是 Server 2022 冒烟机，不替代设计要求的正式发布矩阵。发版前仍应覆盖：

- Windows 10 2004+ 与 Windows 11；Intel/NVIDIA/AMD。
- 有/无物理屏、锁屏/解锁、睡眠恢复、Service/Render 异常重启。
- 两客户端并发、Relay/普通 WS；本次已覆盖 RTC local 与 WebClient `NEED_RECONNECT` 自动重连。
- Windows 10/11 上的安装升级与驱动完整卸载（本次 Server 2022 已通过）。
