# iroh 统一传输层选型评估

日期：2026-10-08。结论：推荐作为 Windows Native 统一传输层的首选候选，先完成隔离原型与双机验收，再替换产品数据面。
本次完成官方资料与源代码核对，没有编译 iroh 原型，没有安装到 90，没有声称 60 FPS、NAT 矩阵或故障切换已经通过。

后续开发以[执行计划](iroh_transport_execution_plan_20261008.md)为准，包含先行媒体修复、多 Relay 调度、Android 消费者迁移及旧栈退役门槛。
本文保留为选型依据，其原型步骤不替代正式阶段验收。

## 1. 为什么调整上一版方案

用户指出现有 P2P/Relay 分裂，而且不能统一承载可靠和不可靠传输。这个问题应在传输层解决。
上一版把可靠控制与 UDP 媒体分别选路，能逐步修补可达性，但仍保留两套网络生命周期。

iroh 提供基于 QUIC 的连接，并处理 NAT 穿透和 Relay 路径；同一连接可承载可靠流和不可靠数据报。
建议业务层只持有一个会话传输对象，底层路径改变不要求业务层换成另一套控制或媒体协议。
这使 RDP 的可靠字节流也能利用打洞后的路径，不再受“只有视频 UDP 能 P2P”的架构限制。
这是对 API 能力的适配判断，不是当前 Pixels RDP 已经支持该路径的声明。
依据：[iroh 项目](https://github.com/n0-computer/iroh)、[连接 API](https://docs.rs/iroh/1.3.0/iroh/endpoint/struct.Connection.html)。

## 2. 核对版本

- 最新 release 查询结果：`v1.3.0`，发布于 2026-09-28；tag commit 为 `0072d7d84b233f9e7185eb676f049beaf557ac03`。
- 查询时 main 为 `13cd54d1d2617b7de9b1878d80feed69ff99cd3a`，不把 main 的新增能力自动视为 release 已有。
- C 绑定参考仓库：`n0-computer/iroh-c-ffi`，查询时 commit 为 `b62ed9d7610828654f8574aa8c3f401f7352eafe`。
- 本机 Rust 为 1.95.0；所查 iroh manifest 要求至少 1.91。满足版本下限不代表 Windows 链接/依赖已验证。
- 原型依赖锁定版本与 Cargo.lock，不使用浮动 Git 分支；交付前核查对应版本许可证、依赖和发布支持政策。

版本来源：[v1.3.0](https://github.com/n0-computer/iroh/releases/tag/v1.3.0)、
[manifest](https://github.com/n0-computer/iroh/blob/v1.3.0/iroh/Cargo.toml)、
[发布政策](https://docs.iroh.computer/about/release-policy)。项目采用 MIT OR Apache-2.0，接入时保留许可证通知。

## 3. 业务映射

以下均为 Pixels 的建议设计：

| 业务内容 | 载体 | 业务约束 |
|---|---|---|
| 会话授权、启动结果、配置与恢复请求 | 独立可靠双向流 | 身份核验与业务授权完成前不发送业务媒体、不注入输入 |
| 键盘、鼠标按钮、控制操作 | 高优先级可靠流 | 保留顺序与防重复，不能因媒体拥塞丢失释放事件 |
| 鼠标移动 | 首版可靠流并合并待发送移动事件 | 后续数据报优化必须使用序号/状态快照，不能先破坏按钮顺序 |
| 视频、音频与语音 | DATAGRAM | 保留帧号、流号、格式代次、恢复类型和播放期限；接收侧重组和去抖 |
| 剪贴板 | 独立可靠流 | 有大小上限与授权；不能阻塞控制 |
| 文件传输 | 独立低优先级可靠流 | 背压、并发与带宽限制；复用现有业务协议 |
| RDP | 可靠双向字节流 | 原生 RDP、保留 FreeRDP 生命周期，不在 Render 解码再编码 |

不同资源会话各有连接与授权；不把所有应用都塞进一条无法隔离的全局连接。相同进程可共享 Endpoint/runtime。
每个 Render 运行时报告自身 EndpointId 和运行时代际，Service 仍只承担现有运行时管理职责。
Native 连接内复用同一个经授权的会话，不再为“加一个文件/音频通道”重复兑换接入凭据。

iroh 的 stream 与 DATAGRAM 是两种语义，不是“TCP 接口”和“UDP 接口”。
调用方按可靠性选通道，不能依赖“Relay 一定收齐数据报”或“数据报不会乱序”。
可靠流彼此隔离排序，但仍共享连接带宽，须限制文件流对实时媒体的竞争。

## 4. 最重要的实时性限制

已核对 v1.3.0 的 Relay 客户端通过 WebSocket 接入，HTTPS 情况下使用 WSS。
Relay 的 QAD/QUIC 地址发现端点不能被当作“已提供 UDP 媒体中继”。
依据：[固定版本 Relay 客户端](https://github.com/n0-computer/iroh/blob/v1.3.0/iroh-relay/src/client.rs)、
[Relay crate](https://github.com/n0-computer/iroh/blob/v1.3.0/iroh-relay/src/lib.rs)。

工程推论：应用 DATAGRAM 不提供可靠交付承诺，但在当前 Relay 路径中，其底层仍经过 TCP。
TCP 丢包重传时，同一路连接后续字节可能一起等待；QUIC 上层多流不能消除外层 TCP 的排队。
因此 iroh 能统一接口与连接维护，不能保证强制 Relay 时具备直接 UDP 的延迟表现。

原型必须分别报告 direct 与 relay 下的帧间隔、延迟和输入响应；不得用 direct 的结果代表 relay。
若 Relay 弱网性能达不到门槛，应先定位队列、降低实时发送预算并评估上游可用传输扩展；不能悄悄恢复产品的第二套 UDP 网络栈。
如果必须另写底层不可靠 Relay 承载，应作为新的选型决策，重新评估维护成本，而不是宣称“直接使用 iroh 即可”。

## 5. 自部署与 Console 集成

可以自部署 Relay，地址发现也可以完全由现有系统提供，不要求购买或使用 n0 公共服务。
依据：[私有网络配置](https://docs.iroh.computer/configuring-networks)、
[自建 Relay](https://github.com/n0-computer/iroh/tree/v1.3.0/iroh-relay)。

建议配置：

- 从固定版本 `presets::Minimal` 起步，显式配置私有 Relay 与端点，不使用 `presets::N0` 的公共发现/Relay 默认值。
  `Empty` 需要显式补齐 crypto provider，不能直接复制不完整示例作为可运行配置。
- Console 下发目标 EndpointId、EndpointAddr、ALPN、资源会话与运行时代际；地址更新通过现有节点通道上报。
- 不启用公共 DNS/pkarr 发布、DHT、公共 Relay 或外部诊断上传。默认关闭自动路由器端口映射；本地发现按产品需要显式启用。
- 完整离线测试包含 Relay 和地址发现的出站审计，防止仅换了 Relay URL 仍访问公共发现服务。
- Render 端点公钥是传输身份，设备码/应用 ID 仍是业务身份。Console 绑定两者；连接后核验远端公钥，再执行会话授权。
- 私钥留在所属进程/设备安全存储，不能分发同一固定私钥给所有安装。克隆节点与运行时重建需防止身份冲突。
- 这里使用 iroh 现有密钥认证，不新建产品 CA，不恢复 PXDC2/PXDD2 等退役授权体系；许可证仍只由 Console 执行。

固定版本 preset 来源：[presets.rs](https://github.com/n0-computer/iroh/blob/v1.3.0/iroh/src/endpoint/presets.rs)。

自建 Relay 准入与业务授权分层：Relay 控制谁可以使用转发资源，Render 检查用户能否进入特定桌面/应用、角色及控制席位。
不把掌握 EndpointId 等同于具有访问权限。原型可使用受控端点名单；产品准入适配须验证动态撤销和现存连接行为。
首次连接禁用带副作用的 0-RTT 业务操作，防止重复启动、输入重放或重复消耗接入授权。

## 6. C++ 接入边界

官方文档给出 Windows x86_64 的 C 绑定，自行构建库，无预编译包承诺。
所查头文件有可靠流和数据报接口，但部分 API 阻塞当前线程；还需核对私有 Relay、状态事件、取消与并发能力是否满足产品。
依据：[C 接入](https://docs.iroh.computer/languages/c)、
[固定 C 头文件](https://github.com/n0-computer/iroh-c-ffi/blob/b62ed9d7610828654f8574aa8c3f401f7352eafe/irohnet.h)。

建议以 Rust `px_transport` 维护 Tokio/iroh 生命周期，向 C++ 暴露窄接口；优先静态链接进 Client 和 Render，避免额外媒体代理进程及循环拷贝。
现成 C 绑定作为原型/接口参考，不能直接把裸指针、阻塞调用和回调上下文传遍业务层。

项目封装使用类型化 RAII 句柄、受控缓冲区与有界事件队列；C ABI 指针只存在于瞬时边界。
Rust 持有真实异步对象，C++ 通过拥有生命周期的适配器读取事件；UI 线程不运行阻塞接收。
验证停止唤醒、排队事件失效、回调内关闭、重复启停及借用缓冲区释放顺序。每包跨 FFI 的调用和拷贝成本必须测量。

C++ focused 构建仍只构建所需目标；新增 Rust 传输库需要明确的 Release 增量入口与依赖关系，不能偷偷触发完整发布构建。
协议差异封装在这个真实外部库边界内，不增加万能网络管理器或可运行的旧协议兼容层。

## 7. 哪些能力仍需 Pixels 实现

- 应用调度、实例 Job 所有权、ACL、占用拒绝、控制席位、许可证计数与访问记录。
- Client 成功接入后向 Panel 发回执；iroh 加密握手成功不等于业务授权完成。
- 默认 10 秒可配置宽限期、显式停止取消恢复、RDP 会话保留。iroh 完全断开后仍需要业务层重新授权/关联。
- 视频分片、播放期限、音频去抖、参考帧恢复与编码码率适配。DATAGRAM 最大可用长度随路径变化，应使用库的实际限制。
- 修复已确认的恢复帧标记丢失与重复 RFI；不能因改用 QUIC 而继续销毁有效的解码参考。
- QUIC 承担传输拥塞控制与 pacing；移除旧 socket 忙等发送与相互竞争的传输控制，保留应用层期限/编码预算。
  FEC 是有成本的应用冗余，可按测量保留或调整，不能固定追加后声称它不占 QUIC 拥塞预算。
- 中文/英文路径状态、实际中断原因、按阶段 FPS、帧间隔 P95/P99、队列龄期和恢复次数。

## 8. Relay 改造边界

iroh Relay 与现有 Pixels `/relay` 房间数据协议不是可互换的服务。
只在 Client 链接 iroh、仍连接现有转发服务，不会自动得到 iroh 的打洞与路径维护。

建议保留产品的 Relay 名称、管理页面、部署角色与 Console 管理职责，改为承载 iroh 数据面并适配现有控制面。
当前“房间/连接数”不能原封不动映射为 iroh 资源会话数；连接数、Relay 实际字节和端点上报会话必须分别统计。
带宽容量、排空与准入要在新数据面验证，不能因为名称相同就宣称现有行为已复用。

仓库原约束要求保留 Relay 数据协议。本选型提案若进入产品迁移，需要明确更新该项约束及相关范围文档；本次评估不删除或替换现有协议。
计划切换时按规则归档退休实现的完整修改前内容，同步更新生产者/消费者，不保留隐藏旧传输回退。
“直连 ↔ iroh Relay”是同一协议下的路径选择，与“iroh ↔ 旧 Native/旧 Relay”两套协议并存不同。

## 9. 原型验收与后续顺序

1. 锁定 iroh 1.3.0、构建 Release 隔离原型和自建 Relay；同时传可靠校验流与模拟 60 FPS 的数据报，验证授权前拒绝业务流量。
2. 本机与 90 分别测试直连、强制 Relay、同一连接 relay→direct→relay 的路径变化；确认可靠流内容校验、会话身份与媒体序号。
3. 用实际游戏/WebView 编码包回放与实时捕获对照；并发文件传输，报告显示 FPS、P99 帧间隔、输入响应、CPU 和 FFI 成本。
4. 注入丢包/重排/带宽突降，验证 DATAGRAM 过期丢弃、恢复帧与编码预算；特别测强制 Relay 的 TCP 丢包排队。
5. 测网卡变化、全 UDP 封禁、Relay 重启、多 Relay、Console 短断、完全离线 LAN、停止/销毁竞态。
6. 通过后接入桌面与云应用；RDP 使用同一传输接口的可靠流，验证原始字节、半关闭/背压及 Windows 会话保留。

回归业务不应因换路径重复启动实例、重复记录访问或多占一路许可。没有真实穿透拓扑测试就不能把公网直连测试标成 NAT 打洞验收。
不能为做原型直接重置用户当前网卡、停止其会话或修改全局防火墙；故障注入放在隔离环境和明确的维护步骤。

浏览器虽可编译 Wasm，但官方当前说明仅通过 Relay，不能直接 UDP 打洞。
我们的 WebView 云应用由原生 Render/Client 承载，不受这个浏览器客户端限制；网页远控仍保留既有 Direct Host WebRTC 范围。
来源：[浏览器限制](https://docs.iroh.computer/languages/wasm-browser)。Android 原生后续复用同一传输契约并单独实机验收。

最终选择条件：可靠/不可靠语义统一、私有基础设施可控、FFI 生命周期合格、强制 Relay 性能可接受。
在这些条件验证前，结论是“适合优先采用并验证”，不是“已经解决当前卡顿”。
