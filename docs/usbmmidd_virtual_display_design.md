# USBMMIDD 虚拟显示器接入设计

日期：2026-08-19（2026-08-22 完成首期实现与 WebClient 全链路验收）
状态：P0-P4 已实现并通过 Windows Server 2022 实机 WebClient 验收；Win10/Win11 发布矩阵待发版验证
范围：Windows 10 2004+/Windows 11，x64；首期最多两块 1920x1080@60Hz 虚拟扩展屏。

## 1. 决策与目标

GammaRay 直接集成 `usbmmidd_v2`（Windows 设备名为 `USB Mobile Monitor Virtual Display`），不自研 IDD 驱动。

**进程所有权原则：**

- `px_service` 是驱动安装、虚拟显示器创建/移除、恢复和清理的唯一所有者。
- `px_render` 不安装驱动、不调用 USBMMIDD 的设备 IOCTL；它只观察 Windows 显示拓扑变化，并重建采集、编码和传输。
- Client 只能向 Render 发业务请求，不能直接控制 Service 或驱动。

该拆分保证 Render 重启不会丢失驱动状态，且驱动/拓扑异常不会进入采集进程的故障域。

```text
Client
  ↓ VirtualDisplayRequest
Render：权限校验、请求转发、采集就绪确认
  ↓ 本机强类型 ServiceMessage
px_service：安装/控制 usbmmidd、状态持久化、拓扑确认
  ↓
usbmmidd_v2 / Windows 显示拓扑
  ↓ WM_DISPLAYCHANGE
Render：防抖重建 DDA/GDI → 配置与视频流更新
```

## 2. USBMMIDD 使用方式

驱动包随产品放在 `dist/usbmmidd_v2/`，包含 `usbmmIdd.inf`、驱动文件及
`deviceinstaller64.exe`。

首次创建虚拟屏时，Service 检测驱动后按需执行：

```text
deviceinstaller64.exe install usbmmidd.inf usbmmidd
```

单屏验证工具可使用：

```text
deviceinstaller64.exe enableidd 1
deviceinstaller64.exe enableidd 0
```

正式多屏控制不依赖上述粗粒度命令，而是移植 RustDesk 的 USBMMIDD 控制封装：

- 设备接口 GUID：`{b5ffd75f-da40-4353-8ff8-b6daf6f1d8ca}`
- IOCTL：`2307084`
- `10 00 00 00`：增加一块虚拟屏
- `00 00 00 00`：移除一块虚拟屏

操作后必须等待 Windows 实际枚举到 `USB Mobile Monitor Virtual Display`，再设置分辨率。
不能把一次 `DeviceIoControl` 调用返回当作“可采集”的成功。

## 3. 状态模型与屏幕标识

USBMMIDD 提供的是同类虚拟显示器集合，不适合作为稳定、可外露的单屏 ID 来源。
`\\.\DISPLAYn` 会因重启、插拔、驱动更新而改变，也不得外露为业务 ID。

Service 持久化逻辑槽位，例如：

```text
usbmmidd-slot-1
usbmmidd-slot-2
```

运行时把槽位映射到当前 `\\.\DISPLAYn`，并仅将逻辑 ID 发送给 Render/Client。

首期创建和删除语义：

- 创建：追加一个空闲逻辑槽位。
- 删除：仅允许删除最后创建的、仍由本产品拥有的槽位。
- 不支持按 `DISPLAYn` 删除任意虚拟屏；该语义在 USBMMIDD 下不可靠。
- Service 重启时，从 Windows 重新枚举驱动与显示器，再将持久化的 desired state 与实际状态对账。

Service 内部状态机：

```text
NoDriver → Installing → Ready(0)
Ready(n) → Creating → Ready(n+1)
Ready(n) → Removing → Ready(n-1)
任意状态 → Faulted → Reconciling → Ready(n)
```

## 4. Service 改造

新增模块建议：

```text
rust_client/px_service/src/
  usbmmidd.rs                 # 驱动包校验、安装、设备接口枚举、IOCTL
  virtual_display_manager.rs  # 状态机、超时、重试、槽位所有权
  virtual_display_store.rs    # ProgramData 下的持久化状态
```

Service 负责：

1. 校验内置驱动包版本、文件完整性和签名；禁止运行时下载驱动。
2. 串行化 install/create/remove 操作，单次操作超时后进入 `Faulted` 并保留诊断信息。
3. 创建后轮询显示器数量、`DISPLAY_DEVICE_ATTACHED_TO_DESKTOP` 状态和目标模式。
4. 在驱动已安装、屏幕已出现后设置 1920x1080@60Hz；若 Windows 拓扑 API 在会话上下文有要求，Service 通过受控 UserProxy 完成该一步，Render 不参与。
5. 仅清理由本产品记录为 owned 的显示器；不依据内存计数盲删其他程序的虚拟屏。
6. 维护拓扑 generation，每次实际改变递增并回传给 Render。

> 实机确认：Windows Service 的 Session 0 无法可靠枚举交互桌面的
> `EnumDisplayDevices/EnumDisplaySettings` 拓扑。因此正式实现由 Service 保持唯一业务所有权，
> 再以随机 nonce 的一次性内部 worker 启动到活动控制台会话；worker 只执行驱动 IOCTL、
> 拓扑枚举和模式设置，结果写入一次性文件，Service 校验 nonce 后才更新 owned 状态。
> 驱动安装仍由 Session 0 内的 Service 完成。
>
> NVIDIA/USBMMIDD v2 实机还确认了一个枚举差异：拓扑稳定后
> `EnumDisplayDevices(NULL, index)` 可能不再返回 USBMMIDD 顶层项，但
> `EnumDisplayMonitors + GetMonitorInfo` 仍能得到活动 `DISPLAYn`，随后对该名称调用
> `EnumDisplayDevices(DISPLAYn, 0)` 可稳定读取 `MONITOR\Default_Monitor` 子设备。
> 实现按此路径枚举候选虚拟屏，并由首次对账的 foreign baseline 保证不会删除既有候选屏。

### 4.1 本机 Service 协议

扩展 `src/px_deps/px_message_new/px_service_message.proto`，只追加枚举值和字段：

```protobuf
enum VirtualDisplayOperation {
  VIRTUAL_DISPLAY_CREATE = 0;
  VIRTUAL_DISPLAY_REMOVE_LAST = 1;
  VIRTUAL_DISPLAY_QUERY = 2;
  VIRTUAL_DISPLAY_RESET_OWNED = 3;
}

message MsgVirtualDisplayRequest {
  string request_id = 1;
  VirtualDisplayOperation operation = 2;
  uint32 width = 3;
  uint32 height = 4;
  uint32 refresh_hz = 5;
}

message MsgVirtualDisplayResult {
  string request_id = 1;
  bool accepted = 2;
  bool topology_changed = 3;
  uint64 topology_generation = 4;
  string logical_display_id = 5;
  string error_code = 6;
  string error_message = 7;
}
```

`RenderServiceClient` 增加请求、响应解析和 `MsgVirtualDisplayServiceResult` 应用事件。
不允许 Render 透传命令行文本、设备 GUID、IOCTL 值或驱动路径。

## 5. Render 与远端协议

远端 `px_message.proto` 新增强类型 `VirtualDisplayRequest/Response`。Render 收到请求后检查：

- 会话拥有远程控制权限，而非只读权限；
- 本机配置显式允许远程创建虚拟显示器；
- 当前请求未超出两屏上限；
- 当前会话是否需要 RTC 重连；
- `request_id` 是否已经处理，保证幂等。

Render 不会在 Service 返回成功后立刻向 Client 宣告可用。它需要等到：

1. 收到显示变化；
2. 采集插件完成拓扑重建；
3. 新虚拟屏的 DDA/GDI 初始化成功；
4. 首帧到达；
5. `ServerConfiguration` 已包含该屏。

上述条件满足后才回 `READY`；否则回 `FAILED` 或 `NEED_RECONNECT`。

### 5.1 显示变化处理

现有 Render 已有 `WM_DISPLAYCHANGE → MsgDisplayDeviceChange → DDA/GDI RestartCapturing` 链路。
该链路必须改为：

- 收到多个 `WM_DISPLAYCHANGE` 后 500ms 防抖；
- 所有 `StopCapturing/StartCapturing/SetCaptureMonitor` 在同一控制线程串行执行；
- DDA 初始化或 `DXGI_ERROR_ACCESS_LOST` 时按受限次数重试；
- 重建前后按 monitor name 做差异比较，刷新输入坐标和虚拟桌面边界；
- 将新 monitor list 和 topology generation 回传给 Client。

## 6. RTC 会话策略

现有 RTC 本地会话在初次协商时决定视频 track 数。运行中创建虚拟屏后，即使 Render 已能采集，也不会自动存在新的 RTP video track。

一期策略：

```text
创建/删除请求
  → Service 已完成拓扑改变
  → Render 已完成采集重建
  → 返回 NEED_RECONNECT
  → Client 自动重连
  → 新会话依据最新屏幕列表协商 tracks
```

二期再评估 SDP renegotiation 或固定预留四条 dormant track。首期不实现前者，避免把驱动接入和 WebRTC 协商风险耦合。

## 7. 打包、更新与卸载

1. 将完整 `usbmmidd_v2` 驱动包加入 `third_party/usbmmidd_v2/`。
2. 修改 `scripts/collect_dist.py`，复制为 `dist/usbmmidd_v2/`。
3. NSIS 安装程序已有管理员权限；安装时按 RustDesk 的方式在驱动目录调用
   `deviceinstaller64.exe install usbmmidd.inf usbmmidd`，等待 PnP 设备节点出现后才报告成功；不在安装时启用虚拟屏。
4. 覆盖安装先检查 `USB Mobile Monitor Virtual Display` 是否已存在，存在则跳过安装命令，避免重复创建 `ROOT\\DISPLAY` 设备。
5. Driver 更新前先拒绝新的 create 请求；待 owned 显示器移除后再更新。
6. 产品卸载先停止业务进程，调用 `stop/remove usbmmidd` 删除设备节点，再精确删除
   Provider 为 Amyuni、原始 INF 为 `usbmmIdd.inf` 的 Driver Store 包；任一步验证失败均中止卸载并保留程序文件以便重试。

## 8. 实施阶段与验收

| 阶段 | 工作 | 验收 |
| --- | --- | --- |
| P0 | 将驱动包纳入仓库与 `dist`；人工验证安装/启停 | Windows 设置中可出现/消失一块虚拟屏 |
| P1 | Service `usbmmidd.rs` 与状态存储；仅本机测试命令 | Service 重启后能识别实际屏幕，操作幂等 |
| P2 | 扩展 ServiceMessage 与 RenderServiceClient | Render 可获得带 request_id 的 Service 结果 |
| P3 | Render 显示变化防抖、DDA/GDI 串行重建、首帧确认 | 新屏可稳定采集，移除后无崩溃/黑屏残留 |
| P4 | Client 控件、远端权限、自动重连 | 远端可创建、重连后看到新屏并正确输入 |
| P5 | GPU/系统/重启/卸载回归与运维日志 | 发布矩阵全部通过 |

必要回归矩阵：Windows 10 2004、Windows 11；Intel/NVIDIA/AMD；有物理屏和无物理屏；锁屏/解锁；睡眠恢复；Render/Service 异常重启；两客户端并发请求；DDA `ACCESS_LOST`；普通 WS/Relay 与 RTC local。

## 9. 非目标

- 不实现隐私模式（将虚拟屏设主屏并关闭物理屏）。
- 不支持 HDR、任意自定义 EDID 或任意删除某一块 USBMMIDD 虚拟屏。
- 不在一期实现运行中 WebRTC track renegotiation。
- 不允许 Client 直接接触驱动安装器或本机 Service 接口。
