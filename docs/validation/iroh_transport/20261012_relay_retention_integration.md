# Relay 容量与排空的真实连接验证（2026-10-12）

上一批修复在新端点候选中排除满员／排空 Relay，同时向运行中的 Render 保留其健康 home。
本轮将 Console 的实际策略接到 Service 生产心跳处理，再更新真实 iroh Endpoint，补齐小数据量的连接证据。
这不是远端安装、跨宿主扩容、视频帧率或长期稳定性验收。晚间卡帧继续留待用户回归，90 和 Android 均未使用。

## 实际结果

本机启动隔离 Console、一次性 PostgreSQL，以及产品 `px_relay_server::iroh::IrohRelay`。
Relay 经现有认证控制连接真实上报，不伪造连接计数；容量为 2，两个 Relay-only Endpoint 建立一条 QUIC 连接和一条双向可靠流。
Node 控制连接发送实际 Endpoint 描述；Service 测试辅助程序调用生产 `handle_command(HeartBeat)`，其返回值直接驱动 Endpoint 候选更新。
没有启动产品 Render、GPU 或远端 Service 进程，报告中的节点遥测是测试夹具，传输与 Relay 数据面是真实连接。

| 阶段 | Console／Service 策略 | 实际数据面 |
|---|---|---|
| 满员 2/2 | 新端点候选空；运行中 home 保留 | 同一 Endpoint 身份、QUIC 连接、可靠流保持；4 次可靠往返及每方向 4 个数据报校验通过；第三端点明确拒绝 |
| Console 排空已生效 | 新候选空；已有 home 仍保留 | 同一连接／流再次完成上述收发；第三端点明确拒绝 |
| 解除排空并关闭两个端点 | 实际上报降至 0/2，新候选重新包含该 Relay | 使用新身份的端点重新上线，随后关闭，最终占用为 0 |

聚焦集成用例通过，25.69 秒。测试期间记录的服务器源文件及两个辅助二进制保持不变；隔离容器和卷均清理完成。
未运行已经通过的 21 项网络回归或双系统安装套件。Windows/Linux Relay 既有证据、跨宿主两台各 2 个端点的证据继续复用。

## 可复现入口

所有编译显式使用 18 jobs，Release 快速增量配置。测试程序不会发布产品 dist 或修改产品配置。

```powershell
cargo build --locked --release --jobs 18 --manifest-path rust_transport/Cargo.toml -p px_transport_probe --bin px_transport_probe
scripts\server_validation\build_relay_policy_fixture.bat
# 使用上一条输出的准确 unittest executable 路径：
pwsh -NoProfile -File scripts/server_validation/relay_retention.ps1 -ServicePolicyBinary build_official/cloud_node/cargo/target/release/deps/px_service-c1bb3805df7c551b.exe
```

Service 夹具默认 ignored，只由集成入口携带实际 Console 响应时运行。入口记录两个二进制 SHA，并在结束时复查；子进程设置 kill-on-drop，等待有上限，错误日志纳入失败输出。
真实传输 probe 为 `2176E322…`，Service unittest helper 为 `E9C4F767…`；完整 SHA 与源文件身份见 [证据索引](20261012_relay_retention_integration.json)。

首次失败 `pg-20261012-170908-41259691` 漏列表 limit 参数，400；第二次 `pg-20261012-171315-36be98ed` 错将直接数组读取为对象字段。
两次均为测试入口错误，均在传输验证之前失败并清理，不能计为产品传输失败或通过。修正后 `pg-20261012-171424-9d531182` 通过；整理最终格式、错误脱敏和可复现入口后，
以最终辅助二进制复跑 `pg-20261012-172047-d50b5571` 再次通过。
原始报告位置与 SHA、失败原因和清理记录保留在索引中。

## 交付与下一项

本轮仅新增测试／诊断入口，没有改生产网络行为或第三方库。上一批已核对的 Server／Node 完整候选继续使用；
开发 Service `A06AF7FE…`、Console `C783635B…` 未变，不重打整包、不向远端传输。
Company 已装 Client `FE8B8257…`、XR Service `636C688E…`／Render `6F9A2D19…` 沿用记录，本轮没有远端命令、临时应用或持久配置变更。

此项容量／排空的本机连接验证已完成。下一未完成步骤仍是可用服务器部署窗口中的 Server-first 配套更新及安装后连接保留确认；
公网 Console 当前在用户排除的 90，本轮没有为了部署操作它，也没有单独更新 XR Service 对接旧 Console。
多 Relay 带宽均衡、未覆盖的 NAT 和长期稳定性继续按现有缺口记录，不以这段小数据量测试宣布完成。
