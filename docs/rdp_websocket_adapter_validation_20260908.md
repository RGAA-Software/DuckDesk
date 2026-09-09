# RDP WebSocket 字节流适配验证（2026-09-08）

## 结论与交付边界

P0 的共享字节流模块已实现、聚焦编译完成，并用本机和 `10.0.0.90` 完成两次自动 RDP 连接。
日志确认协商 AVC444v2，测试进程退出后 `usbtest2` 仍是原有会话 2、原登录时间 16:47、状态为断开。
未使用 Administrator 登录 RDP，没有注销账号、删除 profile、关闭会话内应用或重启远端。

这不是完整产品交付：独立探针使用项目 asio2 WebSocket 和现有 `px::Message` 封装，
尚未接入产品 `WsServer/WsConnection` 的原有连接、SDK 能力分派及 Console/Service 准入。
产品仍坚持复用现有 WebSocket，不增加独立 WS 连接要求。不能将这次实验等同于完整 P0 验收或企业功能上线。

本轮视觉验收未通过：自动首帧文件是黑色过渡帧；本机 `CopyFromScreen` 报“句柄无效”，
没有取得可确认桌面内容的稳定截图。客户端报告收到图形更新、解码/呈现统计，不等于人工可视结果已确认。
后续产品工作区接入须补真实画面、键鼠及通道功能验收。

## 新增代码

- `src/px_deps/px_message/px_message.proto`：增量增加 `kRdpStream = 600` 和 `rdp_stream = 600`，
  不改变旧字段编号。包体包含版本、连接身份、代次、DATA/CLOSE 和二进制 payload，不携带 Windows 密码。
- `src/px_deps/px_rdp/rdp_stream_packet.*`：32 KiB 最大 payload、总包长限制、版本/类型校验、旧代次隔离。
- `src/px_deps/px_rdp/rdp_tcp_bridge.*`：仅连接装配层指定的回环端口，或接管受控本地 socket；
  双向原样传输 RDP 字节，不解析/解码/编码视频，不使用视频丢帧队列。
- `tests/test_rdp_stream.cpp`：17 项 GTest；`tests/rdp_websocket_probe.cpp`：显式启动的 180 秒实机探针。

接收侧默认 2 MiB 排队预算，在任务进入 executor 前即计入，写入 TCP 完成后才释放；
超限关闭整个流并报告错误，不能丢字节后继续。发送侧一次只保留一个读/发请求，必须等 WS 实际写完才继续读。
发送和连接默认各 5 秒超时，可配置。当前接收端是有界队列加终止式过载保护，尚未实现共享 WS 的读暂停或信用流控；
不能把这些保护称为完整的跨端背压。共享 WS 控制消息调度仍待产品路由阶段实现。

绑定值不是授权凭据：调用方必须先验证票据、工作区/节点/实例绑定，再将该连接的 RDP 消息交给模块。
本模块不创建 Windows 用户、不发放占用权、不管理 Render 宽限，也不会注销 Windows 会话。
异步回调仅捕获智能指针，弱引用在执行点 lock；停止幂等，析构不依赖原对象继续存活。

## 实机拓扑与结果

```text
本机 Qt 5 demo → 127.0.0.1:13391 → 本机字节流探针
                                      ↕ WebSocket / px::Message
                              10.0.0.90:13390
                                      ↓
                         90 字节流探针 → 127.0.0.1:13389 FreeRDP proxy → 127.0.0.1:3389 RDS
```

`13391` 仅用于 demo 的本地 TCP 接入；`13390` 是隔离实验入口，不是新产品端口。
测试防火墙只允许本机 `10.0.0.16`；RDP proxy 限定回环监听和固定后端。
探针不实现产品票据/TLS/节点独占，随机 binding 仅用于区分测试流，不可当作认证。
证书忽略仅使用用户允许的内网测试选项；生产自动信任/校验流程仍待接入。

| 时间（北京时间） | 结果 |
|---|---|
| 18:21–18:24 首轮排障 | WS 建立，但桥接回环端口失败；旧配置仅监听 `10.0.0.90:13389`，改成 `127.0.0.1:13389` 后解决 |
| 18:24:53–18:27:50 | 自动连接成功；首轮正常链路本机发 11,841 字节、收 214,721 字节，远端计数互为对应；180 秒截止后正常关闭 |
| 18:29:58–18:32:44 | 新探针、新 binding 自动重连成功，仍是会话 2；本机发 15,182 字节、收 440,182 字节；主动关闭 demo 后远端收到 CLOSE |

第一批（含初次失败排障、重试和清理）不足 10 分钟；第二批不足 3 分钟。
动态分辨率通道在第二次连接的 18:30:13 暂报 unavailable、18:30:25 转为 ready。
这是后续需要处理的通道状态转换，不能据 ready 日志就宣称 resize 功能测试完成。
两次实机都记录 `Got GFX RDPGFX_CODECID_AVC444v2`；没有通过动态负载验证 60fps。
本机连接表确认 demo 只连本地 13391，探针连接远端 13390，没有本机直连远端 13389。

本机探针 EXE 与首轮远端部署 SHA-256 一致：
`0B78F3868B233AA0709A3F1C7D73193BA1D5415EDE49E0FF490A42ABE1B68D4C`。
FreeRDP proxy 固定版本、原生 SSPI 认证依据见 [前序记录](rdp_proxy_validation_20260908.md)。
可提交的无秘密配置模板在 [docs/examples/rdp_proxy_native_probe.ini.in](examples/rdp_proxy_native_probe.ini.in)。

原始私有证据在 `.cache/rdp_proxy_run_6abb5276/` 和 `.cache/rdp_proxy_run_b2780675/`：
`client/rdp_client.log`、`client/freerdp.log`、`ws-client.stdout.log`、`remote-ws.stdout.log` 与 proxy 日志。
这些目录受 ACL 保护并由 Git 忽略；认证日志与设置备份不提交、不整份公开。

## 聚焦验证与复现

```powershell
scripts_build/build_cpp_tests.bat test_rdp_stream rdp_websocket_probe check_cpp_ownership
ctest --test-dir build_official -R '^rdp_stream$' --output-on-failure --timeout 60
```

测试覆盖二进制完整性/顺序、大小和版本、旧代次关闭、发送完成前停止继续读取、重复/迟到完成、
入队预算、发送失败/超时、停止回调重入、挂起读取/完成回调时析构、100 次启停销毁、
回环连接及连接前排队、非法端口失败清理。Windows 定时器测试等待实际事件并设置 1 秒硬截止，避免依赖 10 ms 调度精度。
最终 17 项全部通过，`ctest --repeat until-fail:20` 连续 20 轮通过（合计 3.64 秒）；`check_cpp_ownership` 构建门禁通过。

独立探针只在 `BUILD_TESTING` 下构建，不加入默认 CTest 实机流程。启动时使用：

- 两端 `RDP_PROBE_BINDING` 为同一随机 16–128 字节测试标识；每次重启换新标识。
- 远端 `RDP_PROBE_ROLE=server`、`RDP_PROBE_HOST=10.0.0.90`；先启动回环 proxy，再启动探针。
- 本机 `RDP_PROBE_ROLE=client`、`RDP_PROBE_HOST=10.0.0.90`；启动后 demo 自动连接 `127.0.0.1:13391`。
- 先配置受限防火墙、原生 SSPI、运行时凭证与看门狗；探针只有 180 秒寿命，不会自行部署或清理 proxy。
  不把测试用户名/密码写进命令行、模板或 Git；不要把探针用于生产。

本轮没有运行发布全构建，也没有修改/构建产品 `px_client` 或 Render 可执行文件；
因此不宣称 `build_official/dist` 已获得新 RDP 模式。后续产品 Client 产物必须按仓库规则同步 dist 并核对 SHA-256。

## 清理与后续门禁

已关闭测试 demo 并恢复原设置；移除三次测试的临时计划任务、防火墙规则、运行时配置、SAM、私钥和测试凭证文件。
确认远端 13389/13390 无监听、本机 13391 无监听、测试防火墙规则为 0；保留非秘密二进制和私有日志用于复查。
删除的测试密钥/配置需要下次重新生成；没有删除用户账号、会话或业务文件。

下一步按 [实施计划](rdp_application_mode_implementation_plan.md) 接入既有产品 WS 路由、SDK/Client Qt 6 工作区、
Console 工作区/加密凭证、Service 账号与节点独占、Render 代理监督及断连宽限。
视觉内容、完整输入、resize、音频、剪贴板/文件、匿名票据、忙拒绝、会话恢复和旧模式回归仍需逐项验收。
