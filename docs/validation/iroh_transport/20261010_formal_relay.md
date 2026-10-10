# 正式 Relay 双系统数据面：M1 首批开发

日期：2026-10-10。状态：基础数据面已实现并完成本机 Windows / WSL Linux 验证，M1 整体未完成。

## 本批实现

- `rust_server/px_relay_server` 接入固定 `iroh-relay=1.3.0`，不是将临时 probe 改名发布。
- JSON 明确指定 HTTPS、QAD、TLS 证书/私钥、连接容量；证书相对路径以 JSON 目录为基准。显式 iroh 配置出错即启动失败。
- 连接容量基于真实 connection ID；重复断连不会扣掉另一条连接。排空拒绝新增、保留已有连接。
- 记录真实连接数、接收及转发负载字节；暂不发布没有产品口径的“房间数”。
- 复用 Windows Service 的 stop token，加入 Linux SIGTERM；停止时有十秒退出期限。
- 新增 Linux 聚焦构建入口，Windows 入口支持外置构建目录；两者使用轻量优化 Release，发布开发副本时比较 SHA-256。
- 无新传输 token、ticket 或认证系统，资源会话授权仍由现有业务流程负责。
- Android 移到最后；仍有消费者的旧数据面没有提前归档。实施顺序已写入 AGENTS 和总计划。

配置、运行命令和服务环境变量见 [部署说明](../../../deploy/relay/README.md)。该配置当前仍属开发接入，尚未由 Console 页面下发。

## 验证与实际结果

Windows 和 WSL Ubuntu 20.04 均使用 Rust 1.95.0 / Release；每侧 14 项测试全部通过。
覆盖并发容量、相同 endpoint 的精确断连、排空恢复、证书路径、连续三次启停、UDP 端口冲突后的 HTTPS 清理，以及原有 Relay 回归。
Linux 构建通过显式 `PROTOC=/usr/bin/protoc` 避免协议生成器的 Windows 默认路径，不改外部参考仓库。

使用正式 `px_relay` 进程与现有 `px_transport_probe` 客户端，在校验私有 CA 的 HTTPS 强制中继路径并发传输四条可靠流及数据报：

| 验证 | 可靠数据逐字节校验 | 数据报收回 | 最大数据报往返 | 退出 |
|---|---:|---:|---:|---|
| Windows 最终开发副本 | 6,291,520 字节 | 120 / 120 | 35.941ms | 仅终止本测试拥有的进程；取消/端口释放由单测覆盖 |
| Windows 突发记录 | 6,291,520 字节 | 119 / 120 | 15.105ms | 同上，保留较差记录 |
| Linux 最终开发副本 | 6,291,520 字节 | 117 / 120 | 106.161ms | SIGTERM，退出码 0，存在正常停止日志 |

这些是本机功能检查，不是视频帧率、持续负载或跨宿主性能验收。数据报按不可靠语义记录实际损失，不能要求底层强制可靠；可靠流仍须完整校验。
首次 smoke 脚本误将任意数据报损失当成基础转发失败，现改为保留丢失计数；不会把 119/120 或 117/120 写成无损。
混合突发期间上游出现 `failed to forward packet: Full`，尚未优化。上游 `send_packets_dropped` 明确不包括队列满丢弃，不能用它宣称总丢包数为零。
后续媒体/文件并发验证需结合端点恢复和排队测量；本批不通过扩大队列掩盖积压。

原始结果与日志：

- [Windows](20261010_formal_relay_windows.json)、[Windows 突发记录](20261010_formal_relay_windows_burst.json)、[Linux](20261010_formal_relay_linux.json)。
- 同名前缀 `.relay.log` 为进程日志；`_tests.log` 和 `_build.log` 为各系统测试和构建证据。
- Rust 格式、12 个 Relay Rust 源文件命名检查、Python 语法与 Linux shell 语法检查通过。

## 开发产物

| 系统 | 开发输出 | 与构建输出一致的 SHA-256 |
|---|---|---|
| Windows | `output/px_relay/dev/px_relay.exe` | `19B35D1ADA8B5179748FFDA704CCA3D304C90C46F5D69ADFE5D2C15F61DA7FDF` |
| Linux (WSL) | `/home/chessplayer/pixels-relay-dev-20261010/px_relay` | `B6F14494C56024B9F5D41D8460CC725ECE12B3FA061D9CBB585C8D346427681C` |

构建缓存位于 C 盘与 WSL Linux 文件系统，避免继续挤占 D 盘。
本批未运行全量发布、未安装 Windows Service/systemd/容器、未修改 90 机器。测试进程均已停止。
不把两种系统的本机进程验证冒充 SCM 安装验收、容器验收或跨服务器联通。

## 下一批入口

1. 正式 Console 控制/健康上报、排空状态、Relay 地址描述下发；沿用现有管理身份和资源授权。
2. 接入产品 Setup/Compose 配置生成和运行检查，完成 M1 单 Relay 业务路径。
3. 多 Relay 选择、故障恢复、扩容，待用户新增 Windows/Linux 服务器可用后进行跨宿主验证。
4. Windows/RDP 功能与交付收束后再开发 Android，最后归档旧消费者路径。
