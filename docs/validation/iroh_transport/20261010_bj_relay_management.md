# 正式 Relay 管理接入与 BJ-200M 公网部署

日期：2026-10-10。M1 继续推进，尚未完成实际应用的正式 Relay 整体验收。

## 实现与聚焦验证

- 正式 iroh Relay 复用现有 Console 登记凭据和管理 WebSocket，支持认证、连接/流量上报、维护排空及断开管理连接后停止接收新连接。未增加传输票据或另一套认证。
- Console migration 0039 区分 iroh 的 QAD 端口与旧 Relay 房间计数。iroh 房间数为空，Web 显示“不适用”。
- 节点认证/重连时，从数据库选择近期已认证上报、在线、启用、未排空且至少剩余两个连接位置的 Relay，生成 HTTPS 地址和 QAD 端口列表。静态 Relay 列表不再作为该管理配置来源；正在运行的 Render 热更新留待 M2。
- Windows/Linux Relay 各 14 项测试、管理协议 1 项测试通过。隔离 PostgreSQL 的 3 项存储测试及实际 Console/Relay 管理连接测试通过，包含容量、过期、排空和离线处理。Web 类型检查、2 项 API 测试和生产构建通过。
- 隔离数据库报告：`pg-20261010-110208-4ba5da6e`（migration/cache）、`pg-20261010-110445-2a1668dc`（管理连接）、`pg-20261010-111506-b9eed9b2`（存储）。目录均在 `test-results/server_validation/`。
- Windows 开发二进制和 Console Web 资源已同步并核对 SHA，见 `20261010_relay_management_windows_hashes.json`、`20261010_relay_console_web_hashes.json`。这不代表已升级 90 的 Console。

## BJ-200M 完整容器安装

- 服务器：49.232.233.61，Ubuntu 26.04；安装目录 `/opt/pixels/relay-bj-200m`。通过完整 Docker image + Compose 包部署，未替换独立运行文件。
- 镜像：`pixels-relay:3.2.1-iroh-management-20261010`。
- 镜像 ID：`sha256:36fdf8d2e8d52a680ef165b86c616713edd0b424cb6f647d3b234a1d36a662d3`。
- 安装包 SHA-256：`51748846AE3FB4B58D4FD187DC4B00BAD27D6442B082115E8B2275820E989BEF`。
- Linux 运行二进制 SHA-256：`e901e059760a02a5980badeccaf5dcb66006d83212c06e0618842a96d963a778`，已与容器内文件核对。
- TCP/UDP 均发布 4605。容器以 UID/GID 10001、只读根文件系统运行，日志轮换，重启策略 `unless-stopped`。私钥和凭据只在挂载配置目录，未打包进镜像。
- 使用含实际公网 IP SAN 的私有 CA 证书，客户端显式信任 CA，未关闭 TLS 验证。
- 当前明确使用 `console_managed:false` 做独立公网验证。实际 Console 尚未升级 migration 0039；不能把本机隔离数据库管理测试等同于 BJ 已接入 90 Console。

## 云安全组放行后重试

此前主机本地 TLS HTTP 200，但公网 TCP 4605 超时。用户放行云安全组后，强制 Relay 实测通过：

- 实际选定路径：`relay:https://49.232.233.61:4605/`。
- 数据报往返：120/120；可靠流逐字节校验：6,291,520 bytes。
- 总耗时：4,578ms；最大数据报往返：1,303,150us；最大数据报大小：1,162 bytes。
- 原始记录：`20261010_bj_relay_probe.log`；此前失败记录：`20261010_bj_relay_before_firewall.log`。
- 测试两端在本机 Windows，经公网 BJ Relay 转发。负载包含三条 2MiB 可靠流、控制小流和数据报；**1.303 秒不利延迟保留，功能通过不代表媒体性能通过**。不是 90 游戏或 WebView 验收，也不能据此证明 UDP QAD 端口可用。
- 重试后容器仍 `running`、RestartCount=0，本地严格 TLS 检查 HTTP 200。
- 独立 UDP QAD 探针已通过：对 `49.232.233.61:4605` 完成证书校验的 QUIC 握手并取得服务端报告的公网映射地址，全过程 238ms（包含连接及关闭，不是媒体 RTT）。原始记录 `20261010_bj_qad_probe.log`。因此 TCP 中继和 UDP 地址发现分别有实际协议验证。

## 接续

随后升级实际 Console 完整包，登记并接管 BJ Relay，将管理下发配置用于真实 Windows 应用。继续多 Relay 地址更新、故障恢复与混合负载延迟定位；Windows 服务完整安装与 Android 最后阶段仍未完成。
