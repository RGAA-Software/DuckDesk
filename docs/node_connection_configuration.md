# 节点连接配置

> 2026-09-19媒体边界：节点只上报消费者可直接到达的实际Render host/port。WebRTC使用该同一端点完成Direct Host连接，不再配置
> 独立RTC媒体池、STUN/TURN、ZLMediaKit或经Relay中转的RTC signaling；Relay其余数据能力保持。完整跨端契约见
> [Direct Host WebRTC 与中央媒体能力收缩计划](direct_host_webrtc_scope_plan_20260919.md)。

每台 Cloud Node / Remote 的 Service 使用独立节点身份主动连接 Console。节点身份不再来自 Panel、设备授权或 appkey，
也不读取旧授权缓存。安装包只携带统一程序和端口模板，不包含任何机器 token。

## 1. 首次接入

Console 管理员先创建设备和节点。创建节点接口只在响应中返回一次 `node_token`；数据库只保存摘要，管理列表不会再次显示原文。
在目标 Windows 节点以管理员身份打开 PowerShell，把该 token、Console 节点入口和当前节点的可访问主机名/IP 通过标准输入交给 Service：

```powershell
$configuration = @{
    schema_version = 1
    endpoint = "wss://console.example.com/api/console/node-control"
    node_token = "<Console 返回的 64 字符小写十六进制 token>"
    public_host = "render-01.example.com"
} | ConvertTo-Json -Compress

$configuration | & .\px_service.exe --configure-node-control
```

配置成功后重启 Pixels Service。不要把 JSON 保存为普通文件，不要把 token 放在命令行、TOML、脚本日志或安装包中。
若需要撤销本机身份，先在 Console 轮换/删除节点凭据，再以管理员运行：

```powershell
.\px_service.exe --clear-node-control
```

本机副本位于 Service 数据根下独立的 `node-control` 目录；目录只允许 SYSTEM 和 Administrators，内容使用 machine-scope
Windows DPAPI 加密。加载时拒绝 reparse point、宽权限目录、未知字段、未知 schema、非法 token 和歧义地址。

## 2. 地址约束

- `endpoint` 必须是精确的 `/api/console/node-control` WebSocket URL。生产只允许 `wss://`；`ws://` 仅允许 loopback 开发测试。
- endpoint 不允许 userinfo、query、fragment；token 只在 WebSocket 首个严格 JSON 消息中发送。
- `public_host` 必填，只能是可用的主机名、IPv4 或 IPv6，不含 scheme、凭据、路径或端口，也不能是 unspecified/multicast/broadcast 地址。
- `public_host` 指向当前 Render 节点，不是 Console。当前实现要求公网映射端口与节点监听端口相同；异号映射尚未交付。
- 节点凭据轮换会使旧连接代际失效。新 token 不与旧 token 并行有效，更新本机配置并重启 Service 后才恢复接入。

Console 已提供签名部署证书、短期平台描述和 nonce 持有证明的服务端协议；正式版与私有部署版还必须在 Service 侧消费并持久化对应水位：
Official 只接受官方平台，Customer 不得连接官方平台。该客户端门禁与发行内置信任根仍属于后续实现，当前管理员配置命令、TLS 校验或
发现接口自身都不能替代这项商业验收。

## 3. 高级监听配置

`px_service.toml` 只保存统一端口模板，不保存 Console 地址、公开地址或 token。普通部署不修改它；特殊安装包或端口冲突时，
修改源码模板并重新构建相应产品。Service 本机管理口只绑定 loopback，不为 Console 开放。

```toml
[network]
listen_host = "127.0.0.1"
listen_port = 4603
desktop_port = 4601
panel_port = 4999
discovery_enabled = false
discovery_port = 4604

[applications]
port_start = 4613
port_end = 4998

```

桌面 4601、每个实际应用端口同时承载同号 TCP/WS 与 UDP。应用端口由当前节点自己的 4613–4998 池分配；不同节点可复用同一范围。
管理4603只供同机Panel/Render，Panel 4999按产品需要开放。不存在独立RTC 5000–5031媒体池；Direct Host WebRTC使用对应
Render的实际同号TCP/WS与UDP端口。所有活动范围必须互不重叠。
端口 20371 已完全退役，不是默认值、探测目标或回退端口。

## 4. 上报、对账和命令

Service 认证后上报产品版本、公开地址、桌面端口、应用端口范围和当前可执行能力，然后完成 Console challenge 清单对账。
后续按严格递增 request ID 轮询持久命令，执行前核对 node generation、control epoch、application/deployment/instance revision、
lease 和 deadline。Stop 只作用于完全匹配的 instance/launch；身份不符返回 Unknown，不按 PID、端口或程序路径收编/清扫替代进程。

Game Hook和WebView已进入新命令转换；Direct Host始终使用实际Render端点，不生成RTC Relay参数。Relay既有非RTC数据模式按自身
明确配置工作，不能成为Direct Host失败后的隐藏fallback。
RDP workspace 凭据 envelope 与 GPU 绑定尚未进入新 wire，节点当前必须报告 `rdp=false`，并拒绝意外下发的 RDP/GPU Start。
只有完成真实 Console→Service→Render 测试后，才可把对应能力改为可调度。

节点每 15 秒刷新报告、每秒轮询命令；协议流量不超过 Console 35 秒空闲上限。连接断开后旧 generation 关闭，重连取得新 generation，
先重新对账再继续领取命令。发送成功不是执行成功，只有精确 ACK 才能推进实例状态。

## 5. 验收清单

- 错误 token、轮换前 token、query token、旧路径和非 loopback 明文 WS 均被拒绝。
- 节点报告的 public host/端口/能力与本机配置一致，管理口不会成为客户端连接端点。
- 断线重连先对账；过期 lease、旧 epoch/generation/revision 和重复 request ID 不执行。
- Start/Stop 使用稳定 instance/launch 身份；已停止实例的 Stop 幂等，复用 PID/端口的其他进程不受影响。
- machine DPAPI 文件不含 token 明文，目录 ACL 变化或 reparse point 会 fail closed。
- Official/Customer 部署身份隔离、RDP workspace、GPU、多卡负载与公网实机仍分别留证，不以单元测试代替。
