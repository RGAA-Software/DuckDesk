# Render 网络控制面协程化改造计划

## 1. 文档状态

- 决策日期：2026-09-04。
- 适用范围：`src/px_render`、Render 直接使用的 GammaRay 自维护公共网络/异步组件，以及宿主侧 WebRTC library facade。
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
| 异步基础设施 | `PxAsyncRuntime`、`PxAsyncScope`、one-shot、request registry 已存在 | 90%～100% | 增加 typed bounded async mailbox/event bridge |
| WS ticket/准入/RTC Local 分配 | 主路径已经使用 awaitable | 80%～90% | 收口 request/response 边界、关闭和迟到补偿 |
| Render Service 业务请求 | request ID、应答关联、deadline、断线 FailAll 已存在 | 80%～90% | 接收循环、连接循环和统一 StopAsync |
| 连接与重连 | attempt 内部部分协程化，外部仍由 asio2 callback 驱动 | 35%～45% | generation、backoff、ready/disconnect wait 全部协程化 |
| WS/HTTP server 生命周期 | 业务准入已协程化，底层启动和停止仍混合同步/回调 | 45%～55% | 使用共享 runtime、停止 ingress、drain、response exactly-once |
| UDP 控制面 | 数据面稳定，生命周期和 session timeout 仍偏回调/同步 | 35%～45% | 启停、association、sweep、关闭协程化；逐包路径不改成协程 |
| 应用有序关闭 | 局部 scope 能停止，但根关闭顺序不完整 | 30%～40% | Registry/应用统一 shutdown，WebRTC 静默后卸载 |

Render 网络控制面总体估算已完成 55%～65%，剩余 35%～45%。单人开发和自动化验证预计 15～22 个工程日；真实硬件、弱网和长稳验收不计入该工期。

### 2.1 实施进度（2026-09-04）

本轮已经开始实施，但尚未宣称整个计划完成：

- N0 已完成第一部分：新增 `PxAsyncMailbox<T>`，覆盖有界容量、单消费者、deadline、scope cancellation、close、late publish 和统计；连接 attempt 新增
  `StartAttempt()`、`WaitUntilReady()` 和 generation-aware `MarkReady/FailActive`，旧 callback API 暂时保留为兼容 facade。
- N1 已完成第一部分：RenderServiceClient 的 receive callback 只复制 owned message 并发布到 mailbox，protobuf 解析和请求完成转入 state-lane coroutine；连接
  ready 结果改由 coroutine 等待，不再由 completion callback 执行业务。
- N2 已完成第一部分：WsPanelClient 采用同一 mailbox/connection awaitable 模式；OBS WsIpcClient 的五个 `[&]` 网络 callback 已替换为 weak ownership，IPC wire
  decode 不再使用对象裸指针强转，HookManager callback 改为 weak singleton owner。
- N5 已完成关闭硬门禁的第一步：`RdApplication::Exit` 在停止捕获、编码、组合根和事件路由后，正式调用 `RenderModuleRegistry::StopModules()`，旧插件时代禁止
  StopModules 的 workaround 已删除。
- 尚未完成：asio2 内建 auto-reconnect 仍负责实际重连调度；下一批需要把 backoff/retry owner 收口到 coroutine，随后实现组件 `StopAsync`、WS/HTTP server 共享
  runtime、UDP 控制面和根 absolute-deadline shutdown。

当前自动化结果：公共 mailbox/connection 测试连续运行 20 轮通过；Render quick gate 的 2 个 guard 和 14 个 unit/lifecycle 测试通过；Render lifecycle gate 的
2 个 guard 和 11 个 lifecycle/integration 测试通过。证据目录为 `test-results/render-architecture/20260904-134144-lifecycle`，自动化结论为 GO，不替代最终产品验收。

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

`RenderServiceClient`、`WsPanelClient` 和 OBS `WsIpcClient` 各自使用一个长生命周期 connection coroutine：

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

## 12. 测试方案

### 12.1 公共组件单元测试

- mailbox：push-before-wait、wait-before-push、deadline、cancellation、close、重复 close、late push、满队列、并发 producer、owner expiry。
- connection workflow：ready、connect error、upgrade error、disconnect、旧 generation 事件、deadline、Stop、重复 Start/Stop。
- backoff：上限、jitter 边界、成功 reset、cancellation 立即恢复，使用可注入 clock/random source 保证测试确定性。
- request registry：response-before-timeout、timeout-before-response、duplicate ID、unknown response、disconnect FailAll、shutdown FailAll。

### 12.2 组件生命周期测试

- callback 已排队后 owner 销毁。
- callback 内触发 shutdown。
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
| N2 | WsPanelClient 和 OBS WsIpcClient | 2～3 天 | 无 `[&]`/`[this]`，generation 和 owner-expiry 测试通过 |
| N3 | WsServer/HttpHandler 共享 runtime、准入和关闭 | 3～5 天 | exactly-once、disconnect-during-await、scope-thread shutdown 通过 |
| N4 | UDP 控制面和 session timeout/close | 2～3 天 | receive storm shutdown 及性能基线通过 |
| N5 | Registry/RdApplication 根关闭、WebRTC quiescence/unload | 3～4 天 | StopModules、逆序 drain、DLL unload 顺序测试通过 |
| N6 | 全量回归、故障注入、日志/性能和 dist 交付 | 3～5 天，可与前批并行 | Render runner、哈希和自动化 Go/No-Go 通过 |

总工期按重叠后估算为 15～22 个工程日。若扩展到 Client、Panel、SDK 和其他 common asio2 使用点，需要单独立项，预计额外 4～8 周。

## 14. 每批提交原则

- 一次提交只完成一个可验证的 ownership/lifecycle 边界，不把大规模重命名、网络行为改变和格式化混入同一提交。
- 先加入测试或 architecture guard，再迁移生产路径，最后删除 compatibility adapter。
- 同一能力任一时刻只有一个 active workflow owner；迁移开关不得让旧 callback workflow 与新 coroutine workflow 同时执行副作用。
- 每批记录新增/删除的 callback、scope task、timer、thread 和 outstanding resource，确保复杂度单调下降。
- 回退只切回上一批已验证 workflow；不得回退智能所有权、确定性初始化和 ABI 边界安全修复。

## 15. 完成标准

- Render Service、Panel、OBS IPC 的连接和重连由单一 coroutine workflow 管理。
- WS/HTTP 业务请求、准入、RTC Local 分配、timeout 和 late compensation 全部是 typed awaitable workflow。
- asio2 callback 中没有领域业务、同步等待、重连策略、对象销毁和 borrowed payload 排队。
- UDP 高频数据面性能不回退，控制面启停、session timeout 和关闭可取消、可 drain。
- 正常应用退出调用 StopModules；全部 scope 在根 runtime 停止前完成 drain。
- WebRTC 保持具体动态网络库，不是插件；callback 静默后才卸载 DLL。
- 新增和触及代码零项目 raw pointer、零异步 `[this]/[&]` 捕获、零未初始化状态，资源全部由 RAII owner 管理。
- 代码遵循 composition root、Adapter、typed workflow/state machine 和职责分离；不引入通用 transport/interface 或 service locator。
- 项目 C++ 新代码不超过 150 列，聚焦格式化不改动无关历史代码。
- 单元、集成、生命周期、故障注入、性能、日志隐私和交付哈希门禁全部通过。
