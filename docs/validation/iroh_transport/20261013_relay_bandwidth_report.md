# Relay 带宽候选筛选

本批继续客户端实际连接展示和多 Relay 主线。Console 可为每个 Relay 配置可用转发带宽，结合产品 Relay 的真实计数器决定是否把它交给新端点；健康既有连接保留当前中继。此阶段完成按余量筛选，不声称平均分配、带宽整形或容量压测通过。

## 规则与配置

Relay 管理的编辑框增加可选的可用转发带宽，单位 Mbps，范围1–100000，留空继续按现有连接容量选择。保存、校验、未知负载和余量不足均有中文/英文提示。只修改排空状态时保留当前带宽配置。

Console 用同一认证连接内两次有效上报的转发字节增量与间隔计算 bps。重连、计数器回退、首份上报或间隔超过30秒时标记未知。配置了带宽但负载未知时暂停新分配；达到配置的90%时同样暂停，下一份低负载上报恢复可选。没有配置带宽时不新增这个阻挡条件。

原有身份、禁用、近期上报、连接容量、排空规则仍适用。带宽/容量/排空仅从新端点候选移除 Relay；现有 Service 心跳仍可保留其当前健康 home。禁用、离线和失效上报继续遵循原有规则，不用带宽条件绕过它们。未新增短期令牌、票据或另一套身份验证。

数据库迁移 `0042_relay_bandwidth.sql` 增加配置与速率列及运行角色更新权限。SQLx 元数据通过实际隔离 PostgreSQL 重新生成，未手改缓存；已有迁移不变。Server 应先完成42版迁移再运行新 Console；本次未更新远端数据库。

## 实际证据

- PostgreSQL `relay-nodes`：5个用例通过，包含配置/清除、未知负载、真实接受报告的忙闲切换和现有 home 保留。报告 `test-results/server_validation/pg-20261013-152826-9c6da7ae`。
- SQLx prepare：三套全新数据库、重复初始化和运行角色就绪通过；报告 `pg-20261013-152635-4d20f86d`。
- 真实产品 Relay/Console/Service 心跳/QUIC：2个隔离 loopback 用例通过，报告 `pg-20261013-155845-18edf7a1`。第二用例沿用满额和排空回归；新增带宽用例保留同一端点、连接和可靠流，并收发每方向4个数据报。用例是必要的功能回归，不是视频、网卡或饱和混传压力测试。
- 带宽用例配置1Mbps、连接容量16，实际只连接2个端点并校验2MiB可靠数据。产品计数器上报3.462547Mbps时新候选为空，Service仍保留原 home，原连接双向收发通过；下降至2100bps后恢复新候选。移除占用后实际新端点也重新接入。见 [实际带宽证据](20261013_relay_bandwidth_evidence.json) 和 [满额/排空证据](20261013_relay_retention_evidence.json)。这不等于测得公网最大带宽。
- 初批 `pg-20261013-155631-0046fa06` 新带宽用例通过，但旧用例因重复测试 Relay 名称返回409失败；改成分开的 fixture 名称后整批通过。失败批次保留，不计作验收通过。两批测试容器/卷均按归属清理。
- 网页5文件24用例、目录中英文一致性、ESLint和类型检查通过；开发静态资源与发布目录逐文件SHA一致。严格Clippy的结果单独记录，不混同于行为测试。

严格 `cargo clippy --release --jobs18 --all-targets -- -D warnings` 未全通过：依赖中未改动的 `px_backup/src/control_protocol.rs` 触发 `large_enum_variant`，`px_transport/src/relay_candidates.rs` 触发 `collapsible_if`。随后对所选包运行 `--no-deps`，仍因既有 `storage/src/resource_sessions.rs:descriptor_with_entitlement` 的8参数及 probe `main.rs`/`px_nat_probe.rs` 的折叠分支/多余返回绑定失败。对应文件均无本批逻辑改动；没有加入allow、关闭规则或更改无关源码。本批编译与行为回归通过，但不称严格Rust静态门禁通过。

## 开发交付与限制

Console 使用显式18jobs的 focused Release 构建，发布到 `output/px_console/dev/px_console.exe`；网页发布到其 `static`。运行身份和每个静态文件SHA见 [交付清单](20261013_relay_bandwidth_delivery.json)。客户端三个产品的 Client/Panel 开发 dist 见 [设置交付](20261013_client_settings_delivery.json)。没有新 Setup、远端覆盖、Linux新镜像或版本升级。

Windows/Linux Relay 协议及实现未变，复用既有跨平台安装与双 Relay 恢复证据；本批新带宽用例运行于本机产品 Relay，不冒充本批 Linux/公网现场验证。90/Company/XR/BJ运行身份保持，Android和暂停的短顿/NIC调查不启动。

候选内最终 home 仍由 iroh 探测决定；顺序并不保证不同端点平均分布。此规则不预留带宽，也不限制现有流量，同步启动可能在上报间隔内超出预算；不可当作并发容量保证。统计展示实际 Relay URL，友好名称映射继续列为后续项。

本批代码未提交；配套远端上线和客户端真实界面设置重启验收尚未进行。
