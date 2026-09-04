# Render 网络控制面协程化改造计划

## 1. 文档状态

- 决策日期：2026-09-04。
- 适用范围：`src/px_render`、`src/px_client`、`src/px_panel`、`src/px_deps/px_client_sdk_new`和
  `src/px_deps/px_relay_client` 的 WS/WSS 长连接、GammaRay 自维护公共网络/异步组件，以及宿主侧 WebRTC library facade。
- 基线：Render 内建模块和流程节点插件迁移已经完成；本阶段继续完成网络控制面的业务请求、连接、重连、准入、超时和关闭协程化。
- 约束：继续使用 canonical standalone Asio 以及现有 `PxAsyncRuntime`、`PxAsyncScope`、`PxAwaitable`，不切换 asio3。
- 交付方式：按可独立验证的批次实施，每批必须保持可构建、可测试、可回退；最终由用户在目标环境统一验收。

本计划是 `docs/render_builtin_modules_architecture_upgrade_plan.md` 的网络控制面后续阶段。若两份文档发生冲突，以仓库 `AGENTS.md`、
`docs/cpp_smart_pointer_standard.md` 和本计划中更严格的生命周期约束为准。

## 2. 结论与完成度

此次改造不是重写网络库。asio2 继续负责底层 WS、HTTP 和 UDP I/O，canonical Asio 负责协程调度、deadline、cancellation 和业务工作流。

代码盘点后的基线如下：

| 区域 | 当前状态 | 估算完成度 | 本阶段剩余内容 |
|---|---|---:|---|
| 异步基础设施 | mailbox、delay、scope drain、adapter stop、callback quiescence 已实现 | 100% | 最终验收环境回归 |
| WS ticket/准入/RTC Local 分配 | typed awaitable、deadline、迟到补偿和 scope drain 已接入 | 95%～100% | 实机异常矩阵 |
| Render Service 业务请求 | request registry、FailAll、连接/接收 coroutine 和 StopAsync 已实现 | 100% | 实机 Service 联调 |
| Render 连接与重连 | 三个 Render client 均由统一 supervisor 独占 generation/backoff/adapter reset | 100% | 长稳和真实弱网验收 |
| SDK WS/WSS 长连接 | 统一 supervisor、显式 adapter reset、永久拒绝分流、弱生命周期 callback | 100% | TLS 实服和产品业务联调 |
| Client/Panel 长连接 | Panel、Console、Service 连接共用 supervisor，心跳使用可取消协程 | 100% | 产品业务联调 |
| Relay WS | Render/SDK 注入 canonical runtime，初始离线与在线掉线持续重连 | 100% | 真实 Relay 服务联调 |
| WS/HTTP server 生命周期 | 共享 runtime、准入、停止 ingress 和 absolute-deadline drain 已实现 | 100% | 实机并发验收 |
| UDP 控制面 | sweep/FEC coroutine、StopAsync、receive-storm 并发测试已实现 | 95%～100% | 固定硬件性能对比 |
| 应用有序关闭 | 根 deadline、runtime-thread 续体、WebRTC 静默和安全保留已实现 | 100% | 完整客户端验收 |

计划内生产代码和自动化门禁已完成；剩余工作属于固定硬件性能、真实弱网、长稳和用户最终产品验收，不再包含架构迁移代码项。

### 2.1 实施进度（2026-09-04）

本轮代码实施已经完成，最终产品验收仍由用户在目标环境执行：

- N0 已完成：新增 `PxAsyncMailbox<T>` 和通用 typed `WaitForAsyncDelay()`；连接 workflow 已具备 `StartAttempt()`、`WaitUntilReady()`、
  `WaitUntilDisconnected()`、generation-aware ready/disconnect/failure，以及可取消、可复位、带确定性 jitter 测试的
  `PxReconnectBackoff`。旧 callback API 暂时保留为兼容 facade。
- N1 主体已完成：RenderServiceClient 的 callback 只发布 owned message/typed connection terminal event；单一 state-lane connection coroutine 负责 start、
  ready deadline、disconnect、bounded exponential backoff 和 retry，asio2 auto-reconnect 已关闭；组件提供 absolute-deadline `StopAsync`，关闭 mailbox、
  connection workflow 和全部 pending request 后异步排空 scope。保留的 callback API 只是兼容边界 facade，业务等待、超时和完成状态只由 typed awaitable owner 管理。
- N2 已完成：WsPanelClient、OBS WsIpcClient 已使用同一 workflow/backoff 模型；五个 `[&]` 网络 callback 已替换为 weak ownership，IPC wire
  decode 不再使用对象裸指针强转。OBS IPC 具备统一 `StopAsync`、20 轮 repeated Start/Exit 测试，以及真实 WS server 断开/恢复后的 generation 推进测试。
- N2 重连能力已统一收口到 `PxReconnectSupervisor`：关闭 asio2 auto-reconnect 的三个长期连接不再各自复制循环。监督器负责首次连接失败后的无限重试、
  在线断开重连、连接成功后退避复位、可恢复/永久错误分流、连接 deadline、停止时取消、generation 统计，以及重试前强制 adapter 完整 stopped。
  `StartAttemptIfRunning()` 与 `Stop()` 使用同一生命周期门锁，保证停止开始后不会出现迟到 `async_start`；adapter 未静默时只继续 reset，绝不并发启动下一代。
  自动化覆盖连续启动失败后恢复、在线断开后新 generation、adapter reset 失败、永久错误终止、长退避即时取消及服务启动时离线后上线恢复。
- N2-SDK 已完成：SDK `WsConnection`/`WssConnection` 不再依赖 asio2 的隐式 `set_auto_reconnect(true)`；两者复用相同 supervisor、退避参数、
  adapter stop/reset awaitable 和结构化错误模型。authorization/occupied/session-policy 明确归类为不可重试终态，其余首次离线、upgrade 失败和在线掉线会无限重试；
  每次成功 upgrade 才进入 ready，并通过 generation 区分重连代际。所有 asio2 callback 仅捕获 `weak_ptr`，接收数据在 callback 返回前复制为 owned `Data`；
  异步发送依赖 asio2 `_data_persistence` 在 completion 前保持数据，不再额外分配 `shared_ptr<string>`。真实 WS 自动化覆盖
  “先启动 client、后启动 server”、连续三轮 server stop/start、连接/断开通知、重复启停以及 ready callback 内关闭后再启动。
- N2-Client/Panel/Relay 已完成：Client Panel、Client Console、Panel Service、Panel Console 和 Relay WS 全部显式关闭 asio2
  auto-reconnect，并接入同一 `PxReconnectSupervisor`。Console 每个 generation 重新生成短期 token；Client Panel/Console 心跳从
  asio2 timer 改为 scope 内可取消协程。Relay 生产路径从 Render 组合根或 SDK `MessageNotifier` 显式注入 canonical
  `PxAsyncRuntime`，旧构造入口仅保留进程级兼容运行时。真实 Relay server 测试覆盖初始不可用、两轮重启恢复、generation
  推进和 ready callback 内关闭后再启动。仓库门禁禁止项目所有者目录重新引入 `set_auto_reconnect(true)`。
- N1/N2 关闭时序已加固：三个 client 都会先拒绝新工作、关闭 mailbox/workflow 并向 asio2 adapter 所在线程发布 stop，再取消和等待组件 scope；不再先等
  scope、后停 adapter。Render Service/Panel 每轮 `Start()` 都重建 scope、workflow、mailbox 和 request state，partial start failure 走同一逆序退出路径；drain
  超时和 callback/runtime 线程内发起关闭均输出结构化日志且不释放仍有 outstanding task 的 owner。Render Service/Panel 的 absolute-deadline
  `StopAsync` 已接入应用根关闭任务。公共 `RequestAsioClientStop`/`WaitForAsioClientStopped` 只认可 asio2 的完整 public `is_stopped()` 终态，adapter 和 scope
  两者都静默后才释放或允许下一轮 Start；同步析构 facade 使用同一终态和同一 deadline。为关闭与 `async_start` 同时竞争的窗口增加了 scope drain 后的
  确认性 adapter stop，三个 client 均不会被迟到的连接启动重新激活。
- N3 已完成：`RdContext` 持有的进程级 `PxAsyncRuntime` 通过 `RenderModuleRegistry` 和 `WsTransport` 显式注入 `WsServer`；WS server 只拥有自己的 control
  scope，启动失败会回滚模块启动，退出不再错误地停止共享 runtime。`WsServer::StopAsync` 会先关闭 HTTP/WS ingress，再等待 asio2 server 完整 stopped 终态和
  ticket/admission/RTC allocation scope 排空；`RenderModuleRegistry::StopWsIngressAsync` 已把它接入根 deadline。超时不会释放仍有 outstanding work 的 router owner。
- N4 控制任务迁移已完成：进程级 runtime 通过 `RenderModuleRegistry` 显式注入 `UdpTransport`；心跳清扫和 FEC 窗口不再使用旧
  `PluginContext::StartTimer`，改由 UDP control scope 中两个可取消的周期协程负责。UDP 启动失败会回滚，停止会先取消并排空 control scope，逐包收发、
  分片、pacing 热路径保持同步。`UdpTransport::StopAsync` 会先关闭 UDP ingress，再按根 deadline 等待 server stopped 和 control scope 归零；WS/UDP
  已由 `RenderModuleRegistry::StopNetworkIngressAsync` 一起在捕获与模块 owner 拆除前静默。真实 UDP receive storm 与 `StopAsync` 并发测试已加入，JUnit 记录
  storm packet 数和 stop latency；逐包数据面没有引入 coroutine。
- N5 已完成：`RdApplication::Exit` 建立一个 15 秒 absolute deadline，根 control scope 先并发触发 Service/Panel 停止并 `co_await` 两个 client
  scope 排空；组合根 `RequestStop()` 的 completion 也必须在相同 deadline 内到达，才进入模块 owner 释放阶段。`RenderModuleRegistry::StopModules()` 已恢复，旧插件
  时代禁止 StopModules 的 workaround 已删除。WebRTC DLL→Render 控制事件由 `PxCallbackQuiescence` RAII lease 计数，Stop/Destroy 后必须归零才允许卸载；超时输出
  `WEBRTC_CALLBACK_QUIESCENCE_TIMEOUT` 并把 DLL handle 放入进程期安全保留区。runtime-thread 发起根退出时由独立 RAII dispatcher 执行完整关闭续体，不再等待自身 executor。
- N6 自动化代码已完成：architecture/ownership guard 已覆盖 OBS typed stop、根退出续体和 WebRTC 安全卸载；新增 callback quiescence、UDP storm、OBS lifecycle/
  real-server reconnect 测试。固定硬件性能、真实弱网和长稳数据由最终验收阶段产生。

当前 focused 结果：callback quiescence、UDP receive storm、OBS repeated lifecycle/真实 server reconnect、WebRTC 连续 10 轮 load/start/StopAsync/unload 均通过；
OBS lifecycle 额外连续执行 20 轮通过；SDK WS/WSS、Relay WS、公共 supervisor 和 Opus 并发关闭测试通过。最终 `Mode all` 自动化门禁通过 37 项、
失败 0、跳过 0、unexpected ERROR 0；证据位于 `test-results/render-architecture/20260904-195634-all`。`px_render.exe`、`px_gh.dll`、
`net_rtc.dll`、`net_rtc_local.dll` 的构建树与 `build_official/dist` SHA-256 均一致。该结果不替代用户最终产品验收。

### 2.2 复审加固（2026-09-04）

完成首次交付后的第二轮并发与故障路径复审，并将以下规则作为 N2 的强制补充：

- Relay 外层 `RelayTransportRuntime` 只负责缺失连接的创建和配置变更后的整体替换。普通离线时保留同一个 `RelayServerSdk`，由内部
  `PxReconnectSupervisor` 独占重连；禁止外层按 `IsAlive()==false` 周期销毁 SDK，否则会清零 backoff 和 generation。
- WS/WSS 的 client、scope、supervisor 仅在 scope 已排空且 asio2 adapter 已到达 `is_stopped()` 后释放。任一等待超时必须保留 owner，记录
  `scope_drained`、`adapter_stopped` 和 `outstanding`，后续幂等 Stop 可继续收敛，禁止“记录失败后照常 reset”。
- callback/runtime 线程内发起同步 facade Stop 时，由注入的 `PxAsyncRuntime::DeferBlocking` 提交 RAII 管理且进程退出前可 join 的收尾任务；
  禁止使用 `asio::system_executor` 逃逸组件 runtime/scope 的生命周期。
- 工作线程回调与外部线程同时关闭时，生命周期锁只负责状态转换和 `jthread` 所有权转移，实际 join 必须在锁外完成。析构发生在工作线程时，
  使用 `PxAsyncRuntime::DeferJoin` 把线程 RAII owner 交给统一 joiner，禁止 self-join、detach 和持锁 join。
- adapter callback 发布 ready、disconnect、connect/upgrade failure 时必须携带本轮 generation。公共 supervisor 拒绝旧 generation 信号；进入下一代前
  adapter 必须完整静默，二者共同防止迟到 callback 完成新连接。
- Client Panel、Client Console、Panel Service 和 Panel Console 的启动失败走统一逆序 Stop；回调内 Stop 不得因 `exiting/stopping` 首次置位而让后续
  收尾调用直接返回。Panel Console 在网络状态构造失败后也必须回滚已经建立的 listener、scope、queue 和 blocking runtime。
- Relay heartbeat index 和 Panel Service heartbeat index 是实例级原子计数器，禁止跨实例共享函数静态可变计数器。

本轮新增自动化包括：公共 supervisor 旧 generation 信号拒绝、SDK 真实 WSS server 重启恢复、Relay 外层 runtime 在长期离线期间保持单一 SDK owner
且内部 attempt generation 持续前进，以及 Opus callback shutdown 与外部 shutdown 同时发生的确定性死锁回归。Opus 聚焦测试连续执行 200 轮通过；
最终自动化证据为 `test-results/render-architecture/20260904-195634-all`，结论为 GO，37 项通过、0 失败、0 跳过，产物哈希与日志隐私扫描均通过。

### 2.3 最终复审修正（2026-09-04）

针对“关闭 asio2 auto-reconnect 后是否完整替代原能力”和关闭并发安全，再完成以下收口：

- 所有长期 WS/WSS 连接每次 attempt 都创建独立 asio2 client；callback 捕获不可变 generation，并调用 generation-aware
  `FailActive`、`MarkReady` 和 `MarkDisconnected`。旧代 callback 不能完成或断开新代连接。
- `PxReconnectAdapterSlot<T>` 用短临界区发布和读取当前 adapter；停止流程先取得 shared snapshot，再在锁外请求停止和等待，不持锁跨
  `co_await`，也不依赖可被并发替换的成员地址。
- SDK WS/WSS、Client Panel/Console、Panel Service/Console、Relay WS、Render Service/Panel 和 OBS IPC 均保留首次离线无限重试、在线断线恢复、
  成功后退避复位、永久拒绝终止、adapter 静默后再启动下一代以及停止即时取消能力。
- Render Service/Panel 和 OBS IPC 的 scope、mailbox、request state、supervisor/runtime 通过互斥保护的 shared snapshot 访问；`StopAsync` 的开始和
  最终清理与同步 `Start`/`Exit` 使用同一 operation lock，但绝不把该锁跨越 coroutine suspend point。
- `RelayTransportRuntime` 的 monitor 任务只持有 weak owner 和独立 RAII control state，不再因长生命周期线程永久自持 runtime；新增 owner 未显式 Stop
  时仍能退出 monitor 的回归测试。
- 项目自维护路径的 self-join fallback 统一交给 `PxAsyncRuntime::DeferJoin` 的进程级 RAII joiner；禁止 `.detach()`。仅 vendored ViGEm SDK 和明确排除的
  libwebrtc adapter 保持第三方实现方式。
- 连接失败日志先更新 attempt/backoff 统计再输出；连接恢复时输出被限频错误的 summary 并清零窗口，使 lost/recovered 时间线和计数一致。
- ownership gate 在普通 working-tree、staged 和 clean-checkout 场景都检查有效 diff；async lifetime gate 同时禁止项目代码重新引入 `.detach()`。
- 流程节点插件新增显式 `FlowNodePluginRegistry`，并由 `RenderCompositionRoot` 负责注册、类型校验、创建和枚举，消除“只有接口声明、无生产入口”的空边界。

本节对应的自动化包含 ownership/async/boundary gate、flow-node registry 单元测试、OBS 查询与 Stop 并发、旧 generation 拒绝、初始离线恢复、
在线断线重连、Relay owner 释放、重复 Start/Stop 和完整 Render integration。最终证据为
`test-results/render-architecture/20260904-220229-integration`：自动化总计 37 项通过、0 失败、0 跳过，unexpected ERROR 为 0，日志隐私扫描通过；
`px_render.exe`、`px_gh.dll`、`net_rtc.dll` 和 `net_rtc_local.dll` 的 build-tree/dist SHA-256 全部一致，结论为 GO。Client 和 Panel 也分别通过增量构建、
聚焦发布和 dist SHA-256 校验，Panel 关闭生命周期 6 项通过。硬件、LAN、30 分钟压力及 8 小时 soak 仍由最终验收环境执行。

## 3. 目标和非目标

### 3.1 目标

- 业务请求、准入、deadline、重连、状态转换和关闭以顺序化的 `co_await` 工作流表达，不再散落于回调链。
- asio2 callback 退化成最小边界适配器：锁定弱状态、复制 borrowed 数据、发布 typed event，不执行领域业务和对象销毁。
- 每个网络组件由自己的 `PxAsyncScope` 管理长生命周期任务，应用使用一个明确的根协程按逆依赖顺序停止全部网络能力。
- 请求和连接具有 generation/deadline/cancellation，迟到回调不能完成新一代操作或重新激活已关闭对象。
- 所有项目指针关系使用智能指针或 typed RAII handle；所有状态、成员和局部值确定性初始化。
- 日志能够重建连接、重连、请求、准入和关闭时间线；高频性能点使用有界窗口聚合。

### 3.2 非目标

- 不引入 asio3，不同时升级底层网络库和上层控制模型。
- 不强求代码中完全没有 callback。asio2、Win32、Qt 和 WebRTC ABI 要求的 callback 可以存在于最小 Adapter 边界。
- 不把 `asio::awaitable`、STL 对象或 coroutine frame 导出到 WebRTC DLL ABI。
- 不改造 `src/px_deps/px_webrtc_client` 的 libwebrtc borrowed pointer 契约；只改宿主侧 facade 和排队工作的生命周期。
- 不为视频帧、音频包或 UDP packet 创建逐项 coroutine；高频数据面继续使用直接调用、无阻塞统计和有界队列。
- 不为了使用设计模式创建统一 `ITransport`、service locator、通用插件接口或新的动态发现机制。

## 4. 目标结构

```text
asio2 / WebRTC / Win32 callback boundary
                    |
                    | copy to owned value; weak_ptr.lock(); TryPublish()
                    v
          PxAsyncMailbox<NetworkEvent>
                    |
                    v
           component PxAsyncScope
             |              |
             |              +-- RunReceiveLoop()
             +-- RunConnectionLoop()
                    |
                    +-- WaitReady(deadline)
                    +-- WaitDisconnected(generation)
                    +-- WaitReconnectBackoff(attempt)
                    +-- StopAsync(deadline)
                    |
                    v
       typed service/request/admission workflow
```

网络组件是具体类型。组合根负责构造和注入：

```text
RdApplication
    `-- RenderCompositionRoot / NetworkComposition
        |-- shared PxAsyncRuntime from RdContext
        |-- RenderServiceClient
        |-- WsPanelClient
        |-- WsServer / HttpHandler
        |-- UdpTransport
        |-- RelayTransport
        `-- WebRtcLibrary remote/local facade
```

不允许每个组件再创建私有 `PxAsyncRuntime`。每个组件只创建自己的 scope、mailbox、workflow state 和 typed dependencies。

## 5. 强制 C++ 规则

### 5.1 智能所有权与 RAII

- 新增和本阶段触及的 GammaRay C++ 不得声明、存储、传递、返回或捕获裸指针，包括临时局部变量、成员、容器元素、callback 参数和 `[this]`。
- 独占资源使用 `std::unique_ptr`，共享生命周期使用 `std::shared_ptr`，异步观察使用 `std::weak_ptr` 并在使用点 `lock()`。
- 同步必有对象的 non-owning access 优先使用引用；连续 borrowed 数据使用 `std::span`。引用和 `span` 不得跨 `co_await`、排队任务或线程边界。
- socket、timer、thread、registration、subscription、DLL handle、Win32 handle 和 cancellation connection 必须由 RAII owner 管理。
- 外部 ABI 强制的裸值只允许在最小 Adapter 中瞬时出现，按 `docs/cpp_smart_pointer_standard.md` 标注；不得进入业务对象或异步捕获。
- coroutine 每次恢复后重新 `weak_ptr.lock()`；不得假定进入 coroutine 时存在的 owner 在下一次恢复时仍存活。

### 5.2 确定性初始化

- 所有 scalar、enum、atomic、handle、generation、计数器、deadline、状态和智能指针在声明处使用 `{}` 或明确默认值初始化。
- constructor 完成全部不变量后才允许注册 callback 或启动任务。需要 `shared_from_this()` 的对象必须通过 factory 创建，再显式调用 `StartAsync`。
- 可失败的创建返回 `PxResult<std::shared_ptr<T>>` 或等价 typed result；禁止对外发布半初始化对象。
- partial start failure 必须按已完成步骤的逆序 rollback；析构和 `StopAsync` 对每个 partial state 都安全。
- absent state 使用 `std::optional`、空智能指针或明确 enum，不使用未初始化内存和 undocumented sentinel。

### 5.3 结构和设计模式

- Composition Root：只在组合根创建具体网络对象和连接依赖。
- Adapter：asio2/WebRTC/Win32 callback 只存在于边界 Adapter。
- State Machine：连接、重连和关闭由明确状态及 generation 管理，不使用多个无关系 bool 拼接状态。
- Command/Result：准入和业务请求使用 typed value，不使用 `void*`、`std::any` 或通用消息 service bag。
- Observer：注册返回 RAII token；dispatcher 保存 weak target，dispatch 前做 owned snapshot，允许 callback 内注销。
- Strategy：只用于真实可替换的策略，例如 reconnect backoff；内建 WS/UDP/WebRTC 不因同属网络层而继承统一 transport 接口。
- Source 文件按职责拆分 Adapter、workflow、protocol、domain 和 diagnostics；不建立 catch-all manager。

### 5.4 格式

- 项目 C++ 以根目录 `.clang-format` 为准，列宽上限为 150。
- 一行不超过 150 列时保持一行；超过才换行，或在初始化表、算法分组等明显提升可读性的场景主动分行。
- 不机械格式化未触及的旧代码、generated code 和 read-only third-party tree。

## 6. 公共异步组件

### 6.1 `PxAsyncMailbox<T>`

在 `px_common_new` 增加 typed bounded mailbox，用于把外部 callback 事件安全交给 coroutine。建议接口：

```cpp
template<typename T>
class PxAsyncMailbox final {
public:
    static std::shared_ptr<PxAsyncMailbox<T>> Create(asio::any_io_executor executor, std::size_t capacity);

    [[nodiscard]] PxResult<void> TryPush(T value);
    [[nodiscard]] static PxAwaitable<PxResult<T>> ReceiveUntil(
        std::shared_ptr<PxAsyncMailbox<T>> mailbox,
        std::chrono::steady_clock::time_point deadline);
    [[nodiscard]] bool Close(PxAsyncError reason);
    [[nodiscard]] PxAsyncMailboxStatistics Statistics() const;
};
```

行为约束：

- capacity 必须大于零并在创建时验证；运行期不得无界增长。
- `TryPush` 不阻塞外部 I/O callback。满队列按事件类型执行明确的 reject/coalesce/drop 策略并计数。
- `ReceiveUntil` 支持 deadline 和 scope cancellation；close 后现有 waiter 全部以相同 typed error 恢复。
- complete-before-wait、wait-before-complete、close-before-wait、late publish 和重复 close 都必须确定且幂等。
- mailbox 不保存 owner 裸引用，不让 mutex guard、队列元素引用或 span 跨 `co_await`。
- 优先评估 canonical Asio 的 channel 能否满足当前 MSVC/Asio 版本；若行为或兼容性不足，使用项目自有 bounded queue 和 timer/cancellation signal 实现。

### 6.2 网络事件

按组件定义最小 typed event，不建立全项目万能 variant。连接类事件可共享以下值语义：

```cpp
enum class NetworkConnectionEventKind {
    kInitialized,
    kTcpConnected,
    kProtocolReady,
    kDisconnected,
};

struct NetworkConnectionEvent {
    NetworkConnectionEventKind kind{NetworkConnectionEventKind::kInitialized};
    std::uint64_t generation{0};
    PxAsyncError error{};
};
```

message payload 必须在 callback 返回前复制成 `std::string`、`Data` 或其他 owned value。禁止 mailbox 保存 asio2 `string_view`、session borrowed state 或 callback 栈引用。

### 6.3 连接 workflow

把 `PxConnectionAttemptWorkflow` 的 callback-first API 收口为 awaitable-first：

```cpp
[[nodiscard]] PxAwaitable<PxResult<ConnectionReady>> WaitReadyUntil(std::chrono::steady_clock::time_point deadline);
[[nodiscard]] PxAwaitable<PxResult<DisconnectInfo>> WaitDisconnected(std::uint64_t generation);
void NotifyEvent(NetworkConnectionEvent event);
void Stop(PxAsyncError reason);
```

兼容 callback API 只能作为临时 adapter 调用 awaitable，不得继续成为新业务入口。每个 attempt 生成单调递增 generation；任何旧 generation 的 upgrade、disconnect、timer 和 recv
事件都必须被拒绝并计入 stale-event 指标。

## 7. 业务请求、准入和超时

### 7.1 业务请求

继续使用 `PxAsyncRequestRegistry<T>`：

1. 在 state lane 分配 request ID 并注册 one-shot。
2. 在注册成功后提交 owned outbound message，避免 response 快于 registration。
3. `co_await` registry operation 到 deadline。
4. response loop 按 request ID 完成一次；duplicate/unknown/late response 只计数并按限频规则记录。
5. disconnect、StopAsync 和 fatal protocol error 立即 `FailAll`，不能等待每个请求各自超时。

已有 callback public API 若仍有调用者，保留短期 facade：它 spawn 一个 coroutine，并在调用者要求的 executor 上完成 callback。新调用方只使用 awaitable API。

### 7.2 准入

WebSocket 和 HTTP 准入继续按现有顺序：

```text
snapshot owned request
    -> redeem ticket
    -> validate capability
    -> admit logical session
    -> optional RTC Local allocation
    -> post exactly one response/open action to asio2 session executor
```

不得跨 `co_await` 保存 HTTP request、response、websocket session 或 query `string_view` 的引用。`resp.defer()` 必须被封装为 exactly-once RAII completion state；
client disconnect、timeout、cancellation 和 late acceptance 都有明确补偿。

### 7.3 Deadline 规则

- 所有区间使用 `std::chrono::steady_clock`；API 传递 absolute deadline，嵌套步骤不能各自重新获得完整 timeout。
- timeout owner 是等待结果的 workflow，不是底层 callback。
- deadline 到达后 operation 只完成一次，后续 callback 被视为 late event。
- shutdown cancellation、用户取消和 deadline exceeded 使用不同稳定错误码。
- 日志同时记录 `deadline_ms`、`elapsed_ms` 和最终 `outcome`，不记录 credential 或原始 ticket。

## 8. 连接、重连与接收循环

### 8.1 主连接循环

`RenderServiceClient`、`WsPanelClient`、OBS `WsIpcClient`、SDK `WsConnection`/`WssConnection`、Client/Panel 长连接以及 Relay WS
各自使用一个长生命周期 connection coroutine：

```cpp
PxAwaitable<void> RunConnectionLoop(const std::weak_ptr<ConnectionState>& weak_state) {
    while (const auto state = weak_state.lock()) {
        if (state->scope->IsStopping()) {
            co_return;
        }

        const auto generation = state->BeginGeneration();
        state->adapter->Start(generation);

        auto ready = co_await state->workflow->WaitReadyUntil(state->ConnectDeadline());
        if (!ready) {
            co_await state->backoff->Wait(state->NextAttempt());
            continue;
        }

        state->ResetAttempt();
        co_await state->workflow->WaitDisconnected(generation);
    }
}
```

实际实现不得让 `state` 强引用跨越可能无限等待的 `co_await`；示例只描述顺序，编码时应把需要的 owned dependency 快照缩到单步，并在恢复后重新 lock weak owner。

重连策略必须具备：

- bounded exponential backoff 和 jitter；默认值集中配置。
- 首次连接、协议 upgrade 和已连接后断线的不同错误分类。
- scope cancellation 立即终止 timer，不等待 backoff 完成。
- 网络恢复后 attempt 清零。
- 同一时间只有一个 active generation 和一个 reconnect timer。
- 日志不按每次快速失败无限刷屏，持续失败进入窗口汇总。

### 8.2 接收循环

Render Service 和 Panel WS 的 `bind_recv` 只复制消息并 `TryPush`。一个 `RunReceiveLoop` 在 state lane 顺序完成：

- frame/message size 检查。
- protocol parse。
- request registry completion。
- typed command 分发。
- unknown/duplicate/late message 统计。

控制消息 mailbox 必须有容量和过载策略。不能静默丢弃 request response；当 response queue 无法接受时，连接进入明确的 protocol/overload failure 并触发未完成请求失败。

### 8.3 UDP 特例

UDP packet receive 是高频数据面，不迁移成逐包 coroutine。`bind_recv` 保持轻量同步校验和直接投递，但必须：

- 使用 owned packet buffer 或明确的同步 `span`，不得把 borrowed buffer 排队到 callback 返回之后。
- 不在热路径阻塞、分配无界对象或写逐包日志。
- association、session admission、heartbeat/sweep、rebind/kick 和 stop/drain 使用 control coroutine。
- pacing 算法在单独性能验证前保持现状，不因控制面迁移改变媒体时序。

## 9. 组件实施清单

### 9.1 `RenderServiceClient`

- 将 `bind_init/connect/upgrade/disconnect` 收口为 connection event Adapter。
- 用 `RunConnectionLoop` 统一 ready、disconnect、reconnect 和 cancellation。
- 将 `bind_recv` 改成 owned message mailbox，解析移到 `RunReceiveLoop`。
- 保留现有 request registry，补 duplicate、unknown、late 和 shutdown 指标。
- 新增 `StopAsync(deadline)`：停止接收新请求、关闭 mailbox、FailAll、停止 adapter、等待两个 loop drain。
- 旧 callback request API 只作为 awaitable facade，并标注迁移调用点。

### 9.2 `WsPanelClient`

- 复用相同 connection workflow，不复制一套重连状态机。
- 明确 Panel message 的队列容量和 overrun 策略。
- 断线时清理 generation 相关状态，不让旧 message 完成新连接请求。
- 实现幂等 `StartAsync/StopAsync` 和 repeated start/stop 测试。

### 9.3 `WsServer` 与 `HttpHandler`

- 删除 WS server 私有 `PxAsyncRuntime`，从 `RdContext`/composition root 注入共享 runtime，并创建 server 自有 scope。
- callback 只 snapshot owned request；准入 workflow 在 control lane 执行，response/open 回到 asio2 session executor。
- 封装 deferred response 的 exactly-once RAII 状态，覆盖 client disconnect、deadline、scope cancellation 和 owner expiry。
- `StopAsync` 先拒绝新连接和请求，再取消准入任务，最后停止 asio2 server 并 drain scope。
- 从 scope 自己的 executor 发起 shutdown 时不得同步等待自身；根 shutdown coroutine 负责最终 join。

### 9.4 `UdpTransport`

- 将 start/stop、association timeout、heartbeat sweep 和 session eviction 放入独立 control scope。
- recv/send 热路径保持同步和有界，统计使用 relaxed atomic 或固定容量聚合器。
- StopAsync 先阻止新 association，再停止 timer/recv，等待 in-flight control tasks，最后释放 server。
- 验证 receive storm、session sweep 与 stop 并发，不改变现有 pacing 性能。

### 9.5 OBS `WsIpcClient`

- 首先清除现有 `[&]` callback capture，改成 shared state + callback 捕获 `weak_ptr`。
- 使用统一 connection workflow 处理 connect/upgrade/disconnect。
- 明确 IPC payload owned boundary，禁止借用 asio2 buffer 跨 callback。
- owner 销毁、注入进程退出和 Render shutdown 都通过同一个 StopAsync 路径。

### 9.6 WebRTC library facade

- WebRTC 保持与 WS/UDP/Relay 同层的具体动态网络库，不实现 `ITransport`，不进入流程节点插件集合。
- DLL callback 在 host Adapter 转成 owned value 并交给宿主 scope；现有 ABI pointer 不离开最小兼容实现。
- stop 分成 reject new work、request close、wait callback quiescence、destroy instance、unload DLL 五步。
- callback outstanding 不为零时禁止卸载 DLL；deadline 超时记录 ERROR 并保留 library owner，不能冒险卸载。

### 9.7 SDK `WsConnection` / `WssConnection`

- 两种连接使用同一 `PxReconnectSupervisor` 和 `sdk_websocket_reconnect.h` 策略，不复制重连算法，也不调用 asio2 自动重连。
- start hook 只负责一次 `async_start`；connect/upgrade/disconnect callback 只把 typed 结果交给当前 workflow，成功 upgrade 是唯一 ready 点。
- 每轮失败后必须先 `RequestAsioClientStop` 并等待 `is_stopped()`，确认 adapter 静默后才进入下一 generation；停止与新 attempt 由同一门锁串行化。
- authorization、occupied 和 session-policy 控制信号映射为 `SDK_WEBSOCKET_SESSION_REJECTED` 不可重试错误；网络、DNS、握手和瞬时协议错误保持可重试。
- `Stop()` 先关闭新发送、停止 supervisor 和 adapter，再 drain scope；从 runtime callback 发起时只请求停止，不同步等待自身 executor。
- 二进制和文本异步发送都持有 owned payload 到 completion；回调只锁定 weak owner 更新有界队列水位，不捕获对象裸指针。

### 9.8 Client / Panel / Relay 长连接

- Client Panel、Client Console、Panel Service和 Panel Console 使用组合根已有 runtime；每个连接只拥有 scope 和 supervisor。
- Console 的 upgrade target 在每轮 `bind_init` 重建，避免重连复用过期 token；路由兼容回退仍由同一 generation 工作流管理。
- RelayTransport 只在模块 `Start()` 中创建并启动 runtime 一次；`Tick1Second()` 不再反复执行启动。设置更新通过
  `UpdateSettings()` 直接传递，改变连接参数时才由 Relay runtime 重建连接。
- Relay Client SDK 和 Relay Server SDK 把已有 `PxAsyncRuntime` 一直传递到 `RelayWsClient`；不在每个 Relay 连接内创建线程。
- callback 内关闭时只请求 cancellation，由 RAII 延迟收尾在非 runtime 线程等待 scope/adapter 静默，之后允许安全再启动。

## 10. 根关闭流程

当前正常退出路径必须补齐 `RenderModuleRegistry::StopModules()`，删除旧插件时代“只 StopRouting、不停止模块”的 workaround。最终由根 shutdown coroutine 执行：

```text
1. RdApplication enters stopping; reject new public work
2. Stop network admission and new outbound requests
3. RenderModuleRegistry::StopRouting()
4. Cancel reconnect timers and connection scopes
5. FailAll pending requests with ASYNC_SHUTDOWN
6. Stop WS/HTTP ingress and UDP/Relay association work
7. Stop capture, encoder, sinks, domain services, and concrete transports
8. Await every component PxAsyncScope drain to one absolute deadline
9. Stop and destroy WebRTC remote/local facade
10. Verify callback quiescence, then unload WebRTC DLLs
11. Destroy module owners in reverse dependency order
12. Stop root PxAsyncRuntime last; only then finish application exit
```

统一约束：

- `BeginStop` 原子地把组件从 accepting 改为 stopping，重复调用返回同一停止结果。
- 在 component scope 线程发起停止只能请求 cancellation，不能 `WaitFor` 自己。
- asio2 `stop()` 若可能阻塞或要求特定线程，通过 `AwaitBlockingCall` 或 adapter executor 完成，不能阻塞 control lane。
- deadline 使用根 absolute deadline；后续步骤使用剩余时间，不为每个组件重新给完整 5 秒。
- drain 超时后不能立即 reset 仍可能被 callback 使用的 owner。必须保留安全状态、记录 outstanding task names，并进入受控错误退出。
- `PxAsyncRuntime` 是最后释放的执行资源，logger 必须晚于所有 shutdown ERROR。

## 11. 日志和性能统计

沿用 `docs/render_builtin_modules_architecture_upgrade_plan.md` 的结构化日志规范，并为本阶段固定以下事件：

```text
transport.connection_attempt
transport.connection_ready
transport.connection_lost
transport.reconnect_wait
transport.message_rejected
request.start
request.complete
request.timeout
request.fail_all
session.admission
async.mailbox_overflow
async.scope_drain
render.shutdown_stage
webrtc.callback_quiescence
```

所有 WARN/ERROR 必须包含 `event/component/code/operation/outcome/recoverable`。按上下文增加脱敏后的 `request/connection/session` token、`generation`、`attempt`、
`deadline_ms`、`elapsed_ms`、`queue_depth` 和 `outstanding`。

关键性能窗口：

| 区域 | 指标 |
|---|---|
| connection | attempts、success、timeout、upgrade failure、disconnect、stale event、backoff ms |
| request | started、success、failure、timeout、cancel、late/duplicate response、latency p50/p95/p99/max |
| admission | redeem/admit/allocation latency、deny、timeout、late compensation |
| mailbox | accepted、rejected、coalesced、dropped、depth、high watermark、wait latency |
| shutdown | stage duration、cancelled tasks、outstanding before/after、drain timeout |
| WebRTC | outstanding callbacks、close latency、quiescence latency、unload outcome |

高频 callback 只更新无阻塞计数器。默认每 5 秒输出活跃窗口，持续错误按 30 秒汇总；禁止逐消息、逐 packet 和逐帧 INFO/WARN/ERROR。

`PxReconnectSupervisor` 是连接失败日志的唯一 owner：首个失败立即输出，之后同一持续故障窗口最多每 30 秒输出一次，并携带 `suppressed`、`attempts`、
`successes`、`reconnect_waits` 和 `consecutive_failures`。asio2 connect/upgrade/disconnect callback 只发布 typed failure，不重复写一条错误日志。达到最大
backoff 后的 `transport.reconnect_wait` 最多随 30 秒退避输出一次；ready、terminal 和 shutdown timeout 仍必须立即记录。

SDK WS/WSS 沿用同一事件名并分别使用 `sdk_ws`、`sdk_wss` component。永久拒绝固定输出
`event=transport.connection_terminal code=SDK_WEBSOCKET_SESSION_REJECTED recoverable=false`；日志不得包含 appkey、ticket、authorization header、设备密码或完整会话凭据。
supervisor 的 `connection_attempts`、`successful_connections`、`reconnect_waits`、`adapter_reset_failures`、`consecutive_failures` 和 `generation` 是统一统计源，
不得再从 asio2 内建 auto-reconnect 状态推断重连次数。

## 12. 测试方案

### 12.1 公共组件单元测试

- mailbox：push-before-wait、wait-before-push、deadline、cancellation、close、重复 close、late push、满队列、并发 producer、owner expiry。
- connection workflow：ready、connect error、upgrade error、disconnect、旧 generation 事件、deadline、Stop、重复 Start/Stop。
- backoff：上限、jitter 边界、成功 reset、cancellation 立即恢复，使用可注入 clock/random source 保证测试确定性。
- request registry：response-before-timeout、timeout-before-response、duplicate ID、unknown response、disconnect FailAll、shutdown FailAll。

### 12.2 组件生命周期测试

- callback 已排队后 owner 销毁。
- callback 内触发 shutdown。
- callback shutdown 与外部线程 shutdown 同时竞争，验证锁外 join 且测试进程可在 deadline 内退出。
- 最后一个 owner 在工作线程 callback 中释放时，线程 owner 必须转交 RAII joiner，不得 self-join 或 detach。
- scope executor 内调用 StopAsync。
- unregister during dispatch。
- start 中途失败和逆序 rollback。
- repeated start/stop 至少 100 轮；公共 workflow 可以运行 1000 轮。
- disconnect、timeout、receive 和 stop 同时竞争，只允许一个终态。

### 12.3 WS/HTTP 集成测试

- ticket 成功、拒绝、超时和 late accepted compensation。
- client 在 redeem/admit/RTC allocation 任一步骤中断开。
- response/open exactly once；shutdown 后不得向 session queue 发布新 mutation。
- Render Service/Panel 连接失败、upgrade 失败、快速断连、弱网抖动和恢复。
- SDK WS/WSS 首次服务不可用后上线恢复、真实 server 连续三轮 stop/start、每轮 generation 前进、成功/断开通知恰好一次。
- SDK repeated start/stop、停止与 connect callback 竞争、发送 completion 晚于 Stop、owner 销毁以及永久拒绝后不再发起下一 attempt。
- Relay 初始离线后上线、真实 server 多轮 stop/start、ready callback 内 Stop、延迟 drain 后再启动。
- Relay 外层 runtime 在服务持续离线至少两个 monitor 周期时只创建一个 SDK，内部 generation 继续增长；配置改变才替换外层 owner。
- SDK WSS 使用真实 TLS server 覆盖握手、在线停止、server restart 和新 generation ready，不允许仅用 WS 测试间接代表 TLS 路径。
- request 发出后断线、重连后收到旧 response、旧 generation disconnect 到达。
- receive mailbox overload 触发明确连接失败且所有 pending request 被完成。

### 12.4 UDP 测试

- receive storm 与 StopAsync 并发。
- heartbeat sweep、association update、kick 和 rebind 并发。
- owner 销毁后晚到 recv/send completion。
- packet throughput、pacing delay、CPU 和 allocation 与迁移前基线比较，不允许出现逐包 coroutine 回归。

### 12.5 根关闭和 WebRTC 测试

- 正常 `RdApplication::Exit` 确实调用 StopModules 并按顺序完成。
- 任意阶段 partial start failure 均能关闭。
- WebRTC callback outstanding 为零后才发生 DLL unload。
- callback quiescence timeout 时 DLL 保持加载且日志包含稳定错误码。
- 连续 load/start/stop/destroy/unload 至少 10 轮。
- 根 scope、线程、handle、request、connection 和 route 数在退出后归零。

### 12.6 门禁与交付验证

- 运行 ownership gate，新增行不得出现项目 raw pointer、`[this]`、`[&]` 异步捕获和 manual `new/delete`。
- 增加 initialization/architecture guard，检查未初始化成员、通用 transport/plugin 接口、私有 runtime 和同步 callback wait 不得回归。
- 使用 `build_cpp_common.bat`、`build_cpp_render_network_libraries.bat`、`build_cpp_render.bat` 和对应 focused test runner；不运行 release-only
  `build_official.bat`。
- 改动运行产物同步到 `build_official/dist` 后逐项比较 SHA-256；哈希不一致不得报告可验收。
- 保存 JUnit、日志隐私扫描、性能对比、process metrics 和 artifact hash 到独立 test-results 目录。

## 13. 实施批次与工期

| 批次 | 内容 | 预计工期 | 完成门槛 |
|---|---|---:|---|
| N0 | 基线、mailbox、awaitable connection workflow、公共测试 | 2～3 天 | common focused tests、ownership/initialization gate 通过 |
| N1 | RenderServiceClient connection/receive/request/stop | 2～3 天 | 请求、断线、重连、迟到 response 和 100 轮生命周期通过 |
| N2 | WsPanelClient、OBS WsIpcClient 和 SDK WS/WSS | 2～3 天 | 无 `[&]`/`[this]`，generation、真实重连和 owner-expiry 测试通过 |
| N3 | WsServer/HttpHandler 共享 runtime、准入和关闭 | 3～5 天 | exactly-once、disconnect-during-await、scope-thread shutdown 通过 |
| N4 | UDP 控制面和 session timeout/close | 2～3 天 | receive storm shutdown 及性能基线通过 |
| N5 | Registry/RdApplication 根关闭、WebRTC quiescence/unload | 3～4 天 | StopModules、逆序 drain、DLL unload 顺序测试通过 |
| N6 | 全量回归、故障注入、日志/性能和 dist 交付 | 3～5 天，可与前批并行 | Render runner、哈希和自动化 Go/No-Go 通过 |

总工期按重叠后估算为 15～22 个工程日。本轮已将 SDK WS/WSS、Client/Panel 长连接和 Relay WS 统一纳入；
仓库中只剩第三方 asio2 示例/测试保留其自身 auto-reconnect 用法。

## 14. 每批提交原则

- 一次提交只完成一个可验证的 ownership/lifecycle 边界，不把大规模重命名、网络行为改变和格式化混入同一提交。
- 先加入测试或 architecture guard，再迁移生产路径，最后删除 compatibility adapter。
- 同一能力任一时刻只有一个 active workflow owner；迁移开关不得让旧 callback workflow 与新 coroutine workflow 同时执行副作用。
- 每批记录新增/删除的 callback、scope task、timer、thread 和 outstanding resource，确保复杂度单调下降。
- 回退只切回上一批已验证 workflow；不得回退智能所有权、确定性初始化和 ABI 边界安全修复。

## 15. 完成标准

- Render Service、Panel、OBS IPC、SDK WS/WSS、Client/Panel 长连接和 Relay WS 的连接与重连由公共 supervisor 驱动的
  单一 coroutine workflow 管理。
- WS/HTTP 业务请求、准入、RTC Local 分配、timeout 和 late compensation 全部是 typed awaitable workflow。
- asio2 callback 中没有领域业务、同步等待、重连策略、对象销毁和 borrowed payload 排队。
- UDP 高频数据面性能不回退，控制面启停、session timeout 和关闭可取消、可 drain。
- 正常应用退出调用 StopModules；全部 scope 在根 runtime 停止前完成 drain。
- WebRTC 保持具体动态网络库，不是插件；callback 静默后才卸载 DLL。
- 新增和触及代码零项目 raw pointer、零异步 `[this]/[&]` 捕获、零未初始化状态，资源全部由 RAII owner 管理。
- 代码遵循 composition root、Adapter、typed workflow/state machine 和职责分离；不引入通用 transport/interface 或 service locator。
- 项目 C++ 新代码不超过 150 列，聚焦格式化不改动无关历史代码。
- 单元、集成、生命周期、故障注入、性能、日志隐私和交付哈希门禁全部通过。
