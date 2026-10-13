# CN Relay 低负载持续连接观察

2026-10-13，北京时间21:37:51至21:47:52，CN公网Relay完成600.210秒有界观察。一对本机端点、同一QUIC连接和双向可靠流每5秒交换小报文；121次采样共484次可靠往返、每方向484个数据报，内容校验全部通过。端点身份与可靠流保持，没有采样超时或收发失败。

这是CN新增的低负载连接保持证据，不是视频性能、吞吐上限、复杂NAT或长期稳定性结论。端点配置仅允许`https://49.232.190.218:4605/`，没有直连地址；QUIC数据报经过Relay的TLS通道，不能据此称为UDP直连或UDP媒体中继测试。没有Windows节点迁移或业务应用启动。

完整身份、逐次计时、管理上报及失败样本见[证据JSON](20261013_cn_relay_soak.json)。CN沿用[完整安装报告](20261013_cn_linux_server_report.md)中的Server1.0.56镜像和schema42，没有新安装或产品运行载荷更新。

## 结果及限制

| 项目 | 实际结果 |
| --- | --- |
| 有效观察 | 600.210秒，121次采样 |
| 可靠流 | 484次往返，保持原双向流 |
| QUIC数据报 | 双向各484个，逐报文校验 |
| 最慢整组交换 | 320.105毫秒 |
| 采样期Relay上报 | fresh/ready，2个端点，约2Kbps转发 |
| 结束 | 两个端点释放，probe exit0；Relay上报0连接 |
| 配置与服务 | 19个文件SHA相等；11个容器身份、镜像、启动时间、重启计数相等，均运行 |
| Backup状态 | 所有观察点连接true、告警空 |

整组交换包含4次可靠往返及8个串行数据报收发。320.105毫秒不是单包RTT、视频帧间隔或卡顿时间，不能换算为视频卡顿占比。数据报仅4字节、可靠报文仅4字节，样本不代表动态视频、大文件或多用户容量。

## 工具修正及保留失败

首次探针在90秒自动退出，最后有效采样85.208秒：72次可靠往返及每方向72个数据报成功。stderr明确`retention probe deadline exceeded`，源码证实总期限硬编码90秒；这是测试工具期限不适合持续观察，未归因为Relay断线。失败进程已退出、日志压缩保留。

探针现在接受`--retention-timeout-seconds`，范围30–3660秒；不指定时原retention90秒、distribution120秒保持。缺失值、非法字符串、超范围三个实际调用均拒绝。观察脚本传入观察期限加60秒，在失败时记录报告、退出仅owned进程并压缩日志。产品拨号、认证、恢复及第三方库没有修改。

首次管理读取漏`limit`参数而返回400，补`?limit=100`后通过；失败管理会话也执行注销。该工具错误与数据面观察分开记录。

构建显式18jobs：先构建publication探针并复制到隔离工具目录用于本次观察；随后常规`--release --bin px_transport_probe`生成后续入口所需的fast-release探针。两种SHA在JSON中分别保存，不混称产品发行；Rust格式、Python编译、三个参数拒绝检查通过。

## 复现与收尾

使用仅含单个Relay、`relay_only=true`且包含该私有部署公开CA的配置。不要将环境密码、登记凭据或私钥写入配置或提交Git。

```powershell
cargo build --manifest-path rust_transport/Cargo.toml --locked --release --jobs 18 -p px_transport_probe --bin px_transport_probe
python scripts/server_validation/relay_connection_soak.py --probe rust_transport/target/release/px_transport_probe.exe --configuration <private-config.json> --evidence-directory <new-evidence-directory> --duration-seconds 600 --interval-seconds 5
```

输出目录必须新建，防止覆盖已有失败证据。有效stdout/stderr压缩至1086/20字节，失败stdout/stderr889/186字节，SHA与[证据JSON](20261013_cn_relay_soak.json)相符；原始记录留在`.cache/cn-relay-soak-20261013*`。两个owned探针PID89792/89064均不存在，管理会话已注销。

本批没有使用Company、90或XR，没有编译/发布Client、修改用户设置、重做安装，或恢复短顿/NIC/饱和混传专项。Windows/Linux主线与CN安装继续为已完成；复杂NAT、规模和真正长稳保持未整体验收，Remote独立Setup仍需不与已装产品冲突的Windows目标。Android排除。
