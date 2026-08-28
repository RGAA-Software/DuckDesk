# Parsec VDD 全面切换实施与验收方案

## 1. 文档状态

- 状态：批准实施
- 实施目标：用 Parsec Virtual Display Driver（VDD）全面替换 USBMMIDD
- 产品控制进程：`px_display.exe`
- 产品虚拟屏上限：8 块（按需创建，不预创建）
- 常规重复验收：10 轮
- 适用范围：`px_service`、`px_render`、Windows Client、Web Client、安装包、卸载器、构建与分发流程

## 2. 目标与非目标

### 2.1 最终目标

1. GammaRay 只使用 Parsec VDD 创建和删除虚拟显示器。
2. `px_display.exe` 是唯一驱动控制进程，长期持有驱动句柄并持续发送 heartbeat。
3. `px_service.exe` 保持业务状态权威，监督 `px_display.exe`，保存期望屏幕数并处理恢复。
4. Windows Client 与 Web Client 都能按需创建、删除、切换并采集最多 8 块虚拟显示器。
5. UDP Direct、RTC、Relay 等连接方式共享同一套显示拓扑和采集行为。
6. 新安装包不再携带、安装或使用 USBMMIDD。
7. 升级安装可以精确清理旧版 GammaRay 安装的 USBMMIDD 设备节点和驱动包。
8. 所有实际运行产物进入 `build_official/dist`，并与构建树产物完成 SHA-256 一致性校验。

### 2.2 非目标

1. 不修改或重新编译 Parsec 的闭源驱动二进制 `mm.dll`。
2. 不绕过 Windows 驱动签名校验，不启用 Test Mode。
3. 不修改 WebRTC 第三方所有权模型或插件 instance ABI。
4. 不保留 USBMMIDD 运行时 fallback、双驱动选择或隐藏开关。
5. 当前版本不开放超过 8 块虚拟显示器；该限制与 Render/RTC 的 8 路容量保持一致。
6. 当前版本不实现 HDR。

## 3. 上游边界和许可证

`nomi-san/parsec-vdd` 提供可编译的 C#/WPF 控制器、C/C++ 控制接口和示例，但不包含 Parsec VDD 驱动源码。驱动通过已签名的 `mm.cat`、`mm.inf`、`mm.dll` 安装。

实施时：

1. 上游仓库克隆到 `D:/source/GoCloud/parsec-vdd`。
2. 固定完整 commit SHA、tag、远程地址和逐文件 SHA-256。
3. 保留仓库 LICENSE、核心头文件自带许可证和来源说明。
4. 上游原始代码作为第三方代码处理，不按 GammaRay 规范机械重构。
5. GammaRay 维护的新增代码必须遵守 `docs/cpp_smart_pointer_standard.md`。
6. 驱动二进制的使用和再分发授权独立于开源控制器许可证，在正式对外发布前完成来源和授权确认。
7. 不修改 `mm.dll`、`mm.inf`、`mm.cat`；任何修改都会破坏目录签名。

## 4. 目标架构

```text
Windows Client / Web Client
            |
            v
px_render：通用虚拟屏请求、拓扑 generation、DDA 重建
            |
            v
px_service：授权、幂等、状态持久化、进程监督
            |
            | authenticated local IPC
            v
px_display.exe：驱动句柄、heartbeat、IOCTL、DisplayConfig
            |
            v
Parsec VDD (Root\\Parsec\\VDA)
            |
            v
Windows Display Topology -> DDA -> Encoder -> Client
```

### 4.1 职责边界

`px_service.exe`：

- 保存 `desired_count`、逻辑槽位、拓扑 generation 和最近错误。
- 接收 Render 请求并执行授权、幂等和并发串行化。
- 启动、监督和停止 `px_display.exe`。
- 检测 Worker 异常退出，使用固定间隔重启并恢复期望状态。
- 产品卸载时要求 Worker 删除自有屏幕后再退出。

`px_display.exe`：

- 查询驱动安装状态和版本。
- 打开并以 RAII 管理 Parsec VDD 设备句柄。
- 周期性发送 heartbeat，记录实际间隔和延迟。
- 添加显示器并返回 driver index。
- 严格按 driver index 逆序删除显示器。
- 通过 Windows DisplayConfig 设置扩展模式、分辨率和刷新率。
- 返回结构化错误码、Win32 错误码和可操作说明。
- 支持 `--worker`、`--probe`、`--version` 和诊断日志模式。
- 正式运行不显示托盘窗口和上游 GUI。

### 4.2 运行会话

优先让 `px_display.exe --worker` 在活动交互会话中运行，避免 Session 0 下显示拓扑和 DisplayConfig 的限制。`px_service` 通过现有 ProcessManager/Session Worker 能力启动并监督它。

无活动交互会话时：

- 服务保留 `desired_count`。
- 不报告虚拟屏已就绪。
- 新会话出现后启动 Worker，并恢复期望屏幕数。
- 如果实机证明 Session 0 可以可靠控制驱动和拓扑，可在后续版本简化，但本次验收以交互会话 Worker 为准。

### 4.3 本地 IPC

IPC 必须满足：

- 使用仅本机可访问的命名管道或已有安全 IPC。
- 每次 Worker 启动生成随机 nonce。
- 请求携带 request id，响应携带相同 request id。
- 限制调用方为 GammaRay 服务账户和当前交互用户。
- 协议包含版本号，版本不匹配时拒绝操作并返回明确错误。
- Query、Create、Remove、Reset、Shutdown 操作严格串行。
- 超时后先查询实际拓扑再决定是否重试，避免重复创建。

## 5. 驱动控制模型

### 5.1 Heartbeat

- Worker 长期持有设备句柄。
- heartbeat 安全目标间隔为 50ms，最大允许间隔为 90ms。
- heartbeat 与增删操作在同一串行执行上下文中协调。
- 日志记录最近一次成功时间、最大间隔、连续失败次数和驱动错误。
- 单次失败进入快速状态查询；连续失败则关闭旧句柄、重新枚举设备并恢复。
- Worker 退出前停止接收新请求，逆序删除自有显示器，然后停止 heartbeat 并关闭句柄。

### 5.2 屏幕所有权

- GammaRay 最多按需创建 8 块虚拟屏；控制器和服务不预创建空闲屏幕。
- 每次 Add 保存驱动返回的 index，不根据显示器顺序猜测。
- 逻辑 ID 使用 `parsec-vdd-slot-1` 至 `parsec-vdd-slot-8`。
- 删除采用后进先出，避免 Windows 10 Connectivity 组合回退问题。
- 不认领 Worker 启动前已经存在的外部 Parsec 显示器。
- 如果存在其他控制器，返回 `PARSEC_VDD_FOREIGN_CONTROLLER_DETECTED`，不破坏外部屏幕。

### 5.3 显示模式

每次 Add 后：

1. 等待 Parsec 显示器出现在 Windows 拓扑。
2. 将新屏设置为扩展桌面。
3. 设置请求的分辨率和刷新率，当前默认 `1920x1080@60Hz`。
4. 重新查询确认实际模式。
5. 模式确认成功后才增加 topology generation。
6. 超时或模式不匹配时回滚本次新增屏幕。

## 6. 状态和协议迁移

### 6.1 服务状态

`virtual_displays.json` 升级到新 schema，保存：

- schema version
- backend=`parsec-vdd`
- desired count
- owned slot 列表
- driver index
- width、height、refresh rate
- observed device name
- driver version
- topology generation
- last error

旧 USBMMIDD 状态不直接复用。升级安装清理旧驱动后，将状态初始化为零块 Parsec 屏，避免旧设备名和槽位造成幽灵状态。

### 6.2 Protobuf 和内存消息

- 新增通用 `actual_virtual_display_count`。
- 旧 `actual_usbmmidd_count` 先标记 deprecated，不复用字段号。
- 新版本只写入和读取通用字段。
- 完成同版本全量部署验收后删除旧字段和所有 USBMMIDD 命名。
- 客户端、Render、Service 和 `px_display` 的虚拟屏容量统一为 8；按需创建，不预分配屏幕或窗口。

### 6.3 错误码

至少包含：

- `PARSEC_VDD_NOT_INSTALLED`
- `PARSEC_VDD_UNSUPPORTED_VERSION`
- `PARSEC_VDD_SIGNATURE_INVALID`
- `PARSEC_VDD_DEVICE_OPEN_FAILED`
- `PARSEC_VDD_HEARTBEAT_FAILED`
- `PARSEC_VDD_ADD_FAILED`
- `PARSEC_VDD_REMOVE_FAILED`
- `PARSEC_VDD_TOPOLOGY_TIMEOUT`
- `PARSEC_VDD_MODE_SET_FAILED`
- `PARSEC_VDD_WORKER_UNAVAILABLE`
- `PARSEC_VDD_WORKER_PROTOCOL_MISMATCH`
- `PARSEC_VDD_FOREIGN_CONTROLLER_DETECTED`
- `VIRTUAL_DISPLAY_LIMIT_REACHED`

客户端错误框必须显示失败阶段、原因、建议处理、底层错误和错误码，不允许只显示 `Operation Error`。

## 7. USBMMIDD 清除方案

正式运行代码全部删除：

- USBMMIDD backend 和 Session backend
- USBMMIDD 设备枚举与 IOCTL
- USBMMIDD 运行资产及哈希
- USBMMIDD 安装、修复和运行时 fallback
- USBMMIDD 专有协议字段、日志、审计字段和文档

升级清理是唯一例外：

- 新安装包在停止旧服务后检测旧版 GammaRay 安装的 USBMMIDD。
- 使用精确 FriendlyName、硬件 ID、INF Provider 和产品安装标记确认所有权。
- 删除设备节点和对应 Driver Store 包。
- 不携带旧控制器，不允许重新安装 USBMMIDD。
- 清理失败时升级安装返回非零并给出日志，不进入半切换状态。

## 8. 构建和分发

### 8.1 上游验证构建

- 使用 Visual Studio 2022/MSBuild 编译原版 `ParsecDisplay.csproj`。
- 单独编译 `core/vdd-demo.cc`，验证核心 IOCTL。
- 记录编译器、SDK、commit 和 SHA-256。

### 8.2 产品构建

- 输出统一命名为 `px_display.exe`。
- 将项目接入 `build_official` 的正式构建入口。
- `scripts/collect_dist.py` 必须从权威输出位置复制 `px_display.exe`。
- `build_official/dist` 不得含 `ParsecDisplay.exe`、`vdd.exe` 或 USBMMIDD 文件。
- 构建树与 dist 中 `px_display.exe`、`px_service.exe` 及所有受影响运行资产必须逐项匹配 SHA-256。

### 8.3 安装包

安装顺序：

1. 停止全部 GammaRay 进程和服务。
2. 校验 Parsec 驱动文件哈希和数字签名。
3. 检测已安装驱动版本和设备所有权。
4. 安装或复用受支持的 Parsec VDD。
5. 等待 `Root\\Parsec\\VDA` 状态为 OK。
6. 运行 `px_display.exe --probe`。
7. 清理旧版 GammaRay USBMMIDD。
8. 清理旧虚拟屏状态。
9. 安装并启动新服务。
10. 执行安装后模块和驱动审计。

卸载顺序：

1. 请求删除 GammaRay 自有虚拟屏。
2. 停止 Worker heartbeat。
3. 停止并删除服务及产品进程。
4. 仅当 Parsec 驱动由 GammaRay 安装时删除设备节点和 Driver Store 包。
5. 保留安装前已存在且不属于 GammaRay 的 Parsec VDD。

## 9. 测试计划

### 9.1 自动化单元测试

覆盖：

- 驱动状态和版本解析。
- IOCTL 编解码和错误映射。
- driver index 保存和逆序删除。
- 0 至 8 块的逐级创建/删除边界，以及拒绝第 9 块。
- heartbeat 定时、抖动、连续失败和恢复。
- Worker 销毁时存在排队回调。
- 回调中 Shutdown。
- 连续 Start/Stop。
- 并发 Query/Create/Remove 串行化。
- 请求超时后的实际状态对账。
- 状态文件损坏和 schema 迁移。
- 服务/Worker 重启后的 desired count 恢复。
- 外部 Parsec 屏和外部驱动所有权保护。
- DisplayConfig 成功、超时、模式不匹配和回滚。
- IPC nonce、版本、重复 request id 和非法调用方。
- 相关 GammaRay C++ 代码新增裸指针静态门禁。

### 9.2 本地集成测试

- `px_display.exe --probe` 无驱动、正常驱动、禁用设备、重启待定状态。
- Service 启动 Worker、Worker 主动退出、Worker 崩溃和重新拉起。
- 一次请求响应丢失但驱动已经完成操作时的对账。
- Render 接收拓扑 generation，重建 DDA 且不重启健康传输。
- Windows Client 和 Web Client UI 状态一致。

### 9.3 90机器驱动与签名验收

- 关闭 Windows Test Mode。
- 检查 Secure Boot/驱动签名策略环境。
- 校验 `mm.cat`、`mm.dll`、`mm.inf` SHA-256。
- 检查 Authenticode/catalog 签名链和签名状态。
- 静默安装，确认设备管理器无警告。
- 确认驱动版本与 OS 支持矩阵一致。
- 重启后再次确认设备状态。

### 9.4 10轮端到端验收

每轮：

1. 零块虚拟屏开始。
2. Windows Client 连接90。
3. 创建第一块虚拟屏并确认 `1920x1080@60Hz`。
4. 切换并采集第一块虚拟屏。
5. 创建第二块虚拟屏并确认模式。
6. 依次切换主屏及所有已创建虚拟屏并确认有效画面；容量验收创建至虚拟屏 8，并确认第 9 块被明确拒绝。
7. 删除第二块虚拟屏，确认第一块继续采集。
8. 删除第一块虚拟屏，确认恢复主屏。
9. 断开、重新连接并确认服务和客户端无崩溃。
10. 检查进程、句柄、错误日志和实际显示拓扑。

10轮必须全部通过；任何一轮失败均修复后重新从第1轮开始计数。

### 9.5 客户端和连接矩阵

- Windows Client：游客密码、登录 ticket。
- Web Client：游客、登录。
- UDP Direct、RTC、Relay。
- Windows 与 Web 同时连接。
- 两客户端并发创建/删除。
- 一个客户端修改拓扑，另一个客户端自动收到并恢复。
- 断网、退出、重连、连续连接。

### 9.6 流畅性门禁

- 虚拟屏实际刷新率为 60Hz。
- DDA 稳态采集不低于约 50fps。
- 编码输入稳态不低于约 45fps。
- 稳态帧间隔 P95 不高于 40ms。
- 不出现持续 500ms 以上的绿屏、白屏、黑屏。
- 不出现 USBMMIDD 旧方案约 100ms 的周期性停顿。
- 添加或删除后 10 秒内恢复有效画面。
- 一小时稳定运行无屏幕自动消失、句柄持续增长、服务或客户端退出。

### 9.7 安装和卸载验收

- 全新静默安装。
- 覆盖升级并清理 USBMMIDD。
- 修复安装。
- 静默卸载。
- 安装前已有外部 Parsec VDD 时的保留测试。
- 驱动安装失败时的事务失败和恢复测试。
- 安装后全部模块、服务、插件、Web 资源和驱动审计。

## 10. 验收证据

验收报告必须提供：

- 上游 commit、许可证和来源清单。
- 驱动版本、签名结果和文件哈希。
- 本地自动化测试命令及结果。
- 90机器10轮测试逐轮结果。
- DDA、编码、客户端帧率和帧间隔数据。
- 安装、升级、卸载日志。
- 关键截图和设备枚举结果。
- `build_official/dist` 产物清单和 SHA-256 对照。
- 已知限制、残余风险及是否达到交付门禁。

## 11. 实施阶段

1. 固定上游源码并验证原版构建。
2. 建立 `px_display.exe` 产品工程、Worker 模式、IPC 和测试。
3. 接入 `px_service` 状态机和恢复逻辑。
4. 泛化 Render/Client/Protobuf 字段和错误提示。
5. 删除 USBMMIDD 运行代码和资产。
6. 改造构建、dist、安装、升级清理和卸载。
7. 完成本地自动化测试和整体构建。
8. 在90机器完成静默安装及10轮真实验收。
9. 形成最终验收报告并交付。

## 12. 硬性退出门禁

以下任一条件不满足，不得报告完成：

- 驱动在关闭 Test Mode 的正常 Windows 上签名有效并可加载。
- `px_display.exe` 能持续维持最多 8 块按需创建的虚拟屏，heartbeat 无超时。
- 所有已创建虚拟屏都达到流畅性指标；重点覆盖 1、2、4、8 屏容量点。
- Windows/Web 客户端都能创建、删除、切换和采集。
- 10轮端到端验收全部通过。
- 新安装包不含 USBMMIDD 运行资产。
- 静默安装和卸载闭环通过。
- 运行产物同步到 `build_official/dist` 且 SHA-256 匹配。
