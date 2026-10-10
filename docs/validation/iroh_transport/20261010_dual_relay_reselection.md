# 双 Relay：部署、重选与待接通的地址刷新

本批完成 Windows 90 / Linux BJ 两个实际 iroh Relay，并修复基础端点断线后不及时重选 home Relay 的缺口。**不是应用自动恢复或同一 QUIC 连接迁移验收完成。**

## 当前部署

- 90：39.71.45.66:4605，TCP HTTPS/WebSocket 转发、UDP QAD；沿用 Server 1.0.55 已安装二进制及原登记 `7153ff11-4cdc-40d5-8bea-054fd4f2d6d3`。没有复制新 EXE 或重新登记。
- BJ：49.232.233.61:4605，Linux Compose；沿用登记 `3314f150-27be-42cb-ac2e-bf3e0e68b3c7`，镜像 `pixels-relay:3.2.1-iroh-management-20261010`。
- Console 已下发两个候选，信任两端正常 TLS CA；配置 `relay_only=false`，测试探针单独强制 Relay。没有新增传输凭据。
- 当前 console.env SHA `872CAC22E6D6B4849D7A314BC7F2311D349D3B7960E75CCD57E530F007B26C8D`；备份 `console-before-two-relays-20261010.env`。不要恢复历史 BJ-only 策略。
- 90 relay.env SHA `3DCBB74703D4843427E7C92ADDBC2CB8FFE1E60B98ABB05B7420AAEB172BDBB1`；iroh-relay.json SHA `415C64CB9FDC508068DBE6F43035D8F3FB6B57B2BAE9EBBCFFD33BD2AD283EA8`。
- 首次切换遇到服务账户无法读取 JSON/证书，已自动回滚后补精确 ACL 再切换成功。新安装脚本会为 Relay JSON/证书/密钥分配只读权限，共用 Console TLS 文件时保留双方读取权；独立密钥不额外授权 Console。此安装脚本修正**尚未通过新完整 Server Setup 安装验收**；90 当前 ACL 是已验证的配置调整。

## 根因与修正

检查锁定的本机 Cargo `iroh-1.3.0` 原始源码，未修改第三方：

1. `socket.rs::new_re_stun_timer` 的周期为 20–26 秒；Relay actor 对断开的旧 home 持续重连，并不直接选择另一个候选。
2. `net_report/probes.rs::ProbePlan::with_last_report` 在已有延迟结果时返回空计划。关闭 IP transport 的强制 Relay 场景没有 QAD，后续 HTTPS 增量报告可无候选。实测日志出现 `do_full=false`、空 `relay_latency`、`preferred_relay=None`；不能仅增加等待时间假定会切换。
3. `Endpoint::network_change()` 通知 OS 网络监视器，没有实际接口变化时未触发所需完整报告；实测无效。
4. 公共 `Endpoint::insert_relay` 触发 `RelayMapChange`，被视为需要完整探测。新的 `RelayHealth` 在多候选、home 持续不可用 2 秒后重申已有候选，最多每 5 秒一次。保持端点身份，不关闭健康连接；停止或析构会取消后台任务。不引入公开发现服务。
5. 仅 home 改选仍不足以恢复旧连接：双方改到存活 Relay 后，原 QUIC 连接在本机实验中仍超时。当前 Client 重拨还持有旧 EndpointAddr。需要接通现有 Console/Panel 的授权地址刷新，不能把候选集合当对端最新地址。

## 验证及边界

| 场景 | 结果 |
| --- | --- |
| 原实现，两候选，中断当前 Relay | 原 QUIC 未在窗口内恢复；保留失败证据 |
| 仅通知 network_change | 未恢复 |
| 完整重探测后，保持旧连接 | home 已切换，旧连接仍超时 |
| 新实现，本机两 Relay，停当前一台，取新地址后重连 | 断线观测至重选 4589ms；可靠 6291520 bytes；120/120 数据报；两 EndpointId 不变 |
| 新实现，90→BJ，保持 90 停止直至测试结束 | 断线观测至重选 4995ms；可靠 6291520 bytes；120/120 数据报；实际路径 BJ；最大数据报往返 546904us |

公网测试两端探针运行于本机，Relay 是真实 Windows/Linux 公网服务；不是游戏/桌面重新准入测试，也不代表无抖动。初次公网测试重选成功但整体探针 20 秒期限耗尽（包含等待故障注入），保留失败结果；第二次在同一编排中注入故障、分开记录断线时间并给完整传输窗口后通过。

永久回归入口 `scripts/tests/run_iroh_dual_relay.py`。本机结果 `20261010_dual_relay_local.json`，公网原始日志 `20261010_dual_relay_public.log`。探针明确使用 fresh-address reconnect，不声称保留原 QUIC。

两个 Rust 生命周期测试通过（重复 stop/drop，关闭端点时退出任务）；安装脚本 8 个测试通过；C++ `iroh_transport` / `iroh_frontend` 通过。Client / Render focused Release 已发布开发 dist，40 / 277 文件发布校验通过：

- Client build/dist SHA：`4794AC73E204ABB592F6FEC537B911AF96B912F2FB065654B7E278920812DC20`。
- Render build/dist SHA：`B4ED85B048A67FE5691CF9C7C334199977240862E96C783150A13C3B6685E078`。

90 Cloud Node 仍为上一批 `iroh-desktop-identity-20261010` 完整 Setup；本批健康重选代码仅进入本机开发产物，未手工替换安装目录。

## 接续

下一步接通 Client 在既有资源会话内刷新 EndpointAddr / 再准入，补真正应用故障恢复；候选动态更新及维护排空仍需推进。随后将完整修正打入 Cloud Node / Server Setup，安装验收。Android 最后。

最终 90 Console/Relay/Service Running，BJ ready/fresh；测试探针已退出，90 Relay 已恢复。两候选配置保留，epoch=64。测试没有启动新云应用或修改设备 ACL。
