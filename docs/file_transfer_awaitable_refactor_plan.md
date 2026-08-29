# 文件传输 Awaitable 改造与项目级异步语义演进方案

> 状态：实施中；Phase 0–6 在当前 HTTP/WS 产品拓扑和内网环境内已完成，
> 包含生产 Session、唯一路由、完整性、结构化终态、数据规模矩阵、断点续传和
> 真实 FT DLL 卸载屏障；Phase 7 已完成认证/Service 请求和 RTC 配置更新状态机的
> 第一批迁移，活跃会话真实 revision 广播、Panel/Console 其余请求流和其他高收益
> 工作流仍按批次推进
>
> 日期：2026-08-28
>
> 适用范围：Windows Client、Render、Panel、SDK、FT 插件、网络插件和公共异步运行时
>
> 日常与最终稳定性门槛：关键路径连续 10 轮，不再使用 100 轮门槛

## 1. 目的与结论

本方案解决两个彼此相关的问题：

1. 当前文件传输发送契约存在二义性、重复入队、轮询等待、静默丢包和多传输
   路由重复派发风险，导致进度不动、卡死、内存上涨或文件损坏。
2. 项目中大量跨线程和跨模块回调把一个业务流程拆散在多个对象、线程和队列中，
   错误、取消、超时、对象销毁和状态推进无法形成同一个结构化控制流。

最终决策如下：

- 不因本次改造整体引入 `asio3`。借鉴其 C++20 coroutine 线性流程、组合超时和
  awaitable API 设计，底层使用项目当前 standalone Asio；必要时单独评估从
  Asio 1.29.0 升级到经验证的新版本。
- 文件传输采用“单 Session、单发送队列、单活动路由、异步背压、两阶段提交”。
- 抽取进程级 `PxAsyncRuntime`，增加组件/插件级 `PxAsyncScope`，建立结构化并发。
- 业务工作流使用 `PxAwaitable<T>`；第三方、Qt、音视频和插件 ABI 边界允许保留
  最小叶子回调，但必须立即转换为 awaitable、消息或有界队列，不能继续在回调中
  编排多步业务状态。
- C++20 coroutine 是无栈协程，不存在为每个协程分配一份传统线程栈的问题。
  插件可以使用协程。插件真正的风险是协程帧跨 DLL 生命周期存活：DLL 卸载后若
  仍恢复或销毁该帧，会跳入已卸载代码。必须通过宿主拥有的运行时、插件异步域、
  取消、收敛和卸载屏障解决。
- 不修改 `GetInstance`、加载器句柄、插件实例身份和既有销毁 ABI；不改造
  `src/px_deps/px_webrtc_client` 内部的 libwebrtc borrowed-pointer/Observer 模型。
- 所有新增或本次触及的 GammaRay C++ 代码执行
  `docs/cpp_smart_pointer_standard.md`：无裸指针、无异步裸 `this` 捕获。

本文件是文件传输异步发送与项目 await 语义的后续权威方案。原
`docs/rustdesk_file_transfer_migration_plan.md` 中“同步 `bool SendFunc` 模拟
RustDesk `await send`”的设计由本文件取代；原 RustDesk 协议、路径安全、覆盖、
续传和 UI 迁移结论仍然有效。

## 2. 当前实现与根因

### 2.1 `bool SendFunc` 的含义已经被破坏

`px_ft_engine` 将返回值定义为：

```text
true  = 传输层已接受本消息
false = 传输层完全没有接受本消息，Engine 可以保存并重试
```

客户端当前实现却执行：

```text
先 PostWorkTask，把消息放进下一层队列
再增加/读取 outstanding_sends
超过阈值后返回 false
```

因此一个已经被排队的消息又被 `FtEngine::outbox_` 保存并重试。这个错误与 WS、
Relay 或 RTC 无关，所有协议都会命中。

### 2.2 同一消息存在多层不一致队列

当前链路可能同时存在：

1. `FtEngine::outbox_`；
2. FT 插件 worker/context task queue；
3. Client SDK/Render 插件任务队列；
4. asio2/Relay SDK 发送队列；
5. WebRTC DataChannel 内部 buffer。

这些队列使用不同计数、不同水位和不同失败含义。没有一层能回答“这条消息现在
由谁拥有、是否一定会发出、失败由谁处理”。

### 2.3 轮询等待造成线程堵塞和静默丢弃

Client SDK、标准 RTC 和 Local RTC 都存在 1 ms 轮询队列或
`buffered_amount` 的逻辑。部分路径等待 2 秒后直接返回，调用方无法区分成功、
拥塞超时和断线。工作线程在轮询期间也无法及时处理停止、取消和新消息。

### 2.4 Render 文件消息遍历全部网络插件

`DispatchTargetFileTransferMessage` 当前遍历所有 net plugin。一个 stream 同时
保留 WS 和 RTC 连接时，可能出现重复派发或一条路径成功、另一条失败但结果被
丢失。旧连接迟到的关闭事件还可能影响新一代连接。

### 2.5 回调链造成状态分裂

典型文件发送过程跨越：

```text
UI callback
 -> FtCore task callback
 -> FtEngine SendFunc callback
 -> ClientPluginNetworkEvent callback
 -> SDK callback
 -> RTC thread callback
 -> DataChannel observer callback
```

每一层都拥有部分状态，错误只能写日志或布尔返回，取消无法向下传播，销毁顺序
依赖经验。这是典型的 callback hell，但问题不只是缩进层级，而是业务状态和
生命周期被拆散。

## 3. 插件式结构是否可以使用 coroutine

### 3.1 没有传统“协程栈”限制

C++20 coroutine 是 stackless coroutine：

- 挂起时不会保留一份操作系统线程栈；
- 需要跨挂起点保存的参数、局部变量和 promise 存在 coroutine frame 中；
- 恢复由 executor 调度，可以在明确约束下切换执行线程；
- `co_await` 不会阻塞当前线程。

因此插件 DLL、静态库和 EXE 都可以编译和运行 coroutine。插件数量不会因为每个
协程都分配传统栈而产生线程栈级内存压力。

### 3.2 真正风险是 coroutine frame 的代码归属

如果 coroutine 函数编译在插件 DLL 内，其 frame 的 resume/destroy 路径也依赖
该 DLL 的代码。下面的顺序是禁止的：

```text
插件 coroutine 正在 timer/socket/channel 上挂起
 -> PluginManager 调 OnStop/OnDestroy
 -> FreeLibrary
 -> executor 或迟到 callback 恢复/销毁 coroutine frame
 -> 跳入已卸载 DLL，崩溃
```

所以“停止 io_context”或“把对象设成 nullptr”都不足以保证安全。卸载前必须证明
该插件创建的所有 coroutine 已完成或已被取消并销毁。

### 3.3 DLL 边界规则

1. 不从插件导出接口直接返回 `asio::awaitable<T>`。
   - 避免让 coroutine frame、promise、allocator 和异常跨 DLL ABI 暴露；
   - 避免宿主在插件卸载后仍持有一个由插件代码实现的 awaitable。
2. coroutine 编排保留在创建它的模块内部。
3. 宿主向插件提供共享的 executor、取消域和叶子异步服务。
4. 既有 callback ABI 通过 `asio::async_initiate` 或项目 adapter 包成 awaitable；
   callback 只负责完成一次 operation，不再编排下一步业务。
5. 所有插件 runtime artifact 必须与宿主同批构建、同批发布，禁止混用旧 DLL 和
   新 EXE。

### 3.4 `PxAsyncScope` 与插件卸载屏障

每个插件实例获得一个宿主创建、宿主持有的 `PxAsyncScope`：

```text
PxAsyncRuntime（进程级，宿主拥有）
  └─ PxAsyncScope（插件实例级）
       ├─ cancellation source
       ├─ accepting flag
       ├─ outstanding operation count
       ├─ completion barrier
       └─ diagnostics: task name/start time/state
```

插件启动 coroutine 时必须经 Scope：

```cpp
scope->Spawn("ft-session", [state = session_state]() -> PxAwaitable<void> {
    co_await RunFtSession(state);
});
```

禁止直接使用无人管理的 `asio::co_spawn(..., asio::detached)`。

插件停止顺序：

1. `BeginStop()` 原子地拒绝新 operation；
2. 发出 cancellation；
3. 关闭 async channel、timer 和 transport 等待；
4. 允许 coroutine 从挂起点恢复并执行清理；
5. 等待 outstanding operation count 归零；
6. 清理插件业务对象；
7. 调用既有 `OnDestroy`/释放实例；
8. 最后才允许 `FreeLibrary`。

如果超时，禁止强行卸载 DLL。宿主记录未收敛任务的名称、挂起点、持续时间和
session id，并把插件标记为 shutdown failed。卸载等待不得发生在该 Scope 自己的
executor 线程上，避免自等待死锁。

### 3.5 coroutine frame 使用约束

- coroutine 中禁止保存裸指针、裸 `this`、借用引用、`string_view`、`span` 或指向
  外部可变 buffer 的地址跨越 `co_await`。
- 新增异步对象的成员 coroutine 默认禁止。成员 coroutine 会把隐含 `this` 保存
  到 frame，违反项目生命周期规则。使用 free/static coroutine，并按值传入
  `shared_ptr<State>`：

```cpp
static PxAwaitable<void> Run(std::shared_ptr<FtSessionState> state);
```

- 大型 byte buffer 不直接作为 coroutine 局部数组，使用 `shared_ptr<Data>` 或
  move-only buffer owner；避免 coroutine frame 膨胀。
- 禁止持有 mutex、Qt model lock 或插件注册表锁跨越 `co_await`。
- 禁止无界递归创建 coroutine；循环流程用单 coroutine loop。
- 顶层 Spawn 必须观察异常并转换成结构化错误，不能让异常逃逸或被 detached
  静默吞掉。

## 4. 项目级 Awaitable 基础设施

### 4.1 抽取 `PxAsyncRuntime`

当前 `MessageNotifier` 内已有私有 `AsioRuntime`，但它只服务通知器。下一步将通用
能力抽取为 `px_common_new` 的公开、进程级组件：

```text
PxAsyncRuntime
  ├─ control io_context + strand
  ├─ state io_context + strand
  ├─ bounded worker executor
  ├─ work guards
  ├─ thread ownership
  ├─ drain/cancel shutdown
  └─ runtime statistics
```

`MessageNotifier` 改为使用该 runtime，但继续保留现有 pub/sub 语义。不能把媒体帧
或所有通知强行转换成 coroutine。

### 4.2 公共类型

建议新增：

```cpp
template<class T>
using PxAwaitable = asio::awaitable<T>;

template<class T>
class PxResult;

class PxAsyncRuntime;
class PxAsyncScope;
class PxCancellationSource;

template<class T>
class PxAsyncChannel;
```

`PxResult<T>` 承载明确错误：

```text
cancelled / timeout / disconnected / backpressure_timeout /
protocol_error / io_error / authentication_error / internal_error
```

业务层不使用 `bool` 表示多种结果。Asio/system exception 在模块顶层转换为
`PxResult`，不跨插件边界传播。

### 4.3 执行器规则

- 每个连接/session 有独立 strand，所有可变协议状态只在该 strand 上访问。
- UI 更新通过 Qt executor adapter 回到 UI 线程。
- 文件枚举、读写、SHA-256 等磁盘/CPU 工作进入 bounded worker executor，完成后
  回到 session strand。
- RTC/WS/Relay 的第三方 callback 只捕获 `weak_ptr`，立即 post 到所属 strand。
- `co_await` 前后不能假定线程不变，除非 operation 明确绑定同一个 executor。
- 跨 lane 顺序必须通过显式 await 或 completion message 表达，不能依赖“通常先到”。

### 4.4 结构化并发规则

- 每个长期任务必须属于 Runtime、Plugin Scope、Connection Scope 或 Session Scope。
- 父 Scope 停止会取消全部子任务。
- 子任务结果必须被 await、join 或由 Scope completion handler 收集。
- 禁止 fire-and-forget；统计/日志类一次性工作也必须属于 Scope。
- deadline 和 cancellation 是 API 的一部分，不由每个调用点临时拼装 sleep loop。

### 4.5 插件边界的 callback-to-awaitable 适配

由于 FT 插件和 net plugin 可能位于不同 DLL，本方案不让一个模块创建的
`asio::awaitable` 直接返回给另一个模块。边界维持一次完成语义的宿主服务：

```text
FT plugin coroutine
  -> 插件内 FtTransportAwaitAdapter
  -> 宿主 FtTransportService.Submit(message, completion)
  -> 目标 net plugin
  -> completion 恰好完成一次
  -> adapter 恢复 FT plugin coroutine
```

`completion` 必须是由宿主 Scope 跟踪的智能所有权状态，结果只能从 pending 原子地
转换为 accepted/cancelled/error 之一。插件 Stop 后 completion 即使迟到也只能释放
资源，不能恢复已取消 Scope 中的业务代码。这个叶子 callback 是 ABI/第三方事件
适配，不是业务 callback 链。

如果实现过程中发现现有插件 ABI 无法安全承载该 completion，优先把
`FtTransportService` 放入宿主进程并通过既有 PluginContext 服务访问；不得为了
省事跨 DLL 暴露 coroutine frame，也不得改变 `GetInstance` 和加载器所有权模型。

## 5. 文件传输目标架构

```text
FT UI / remote protocol input
             |
             v
       FtSession command channel
             |
             v
  FtEngine（确定性协议状态机）
             |
       PrepareOutbound
             |
             v
  FtAsyncSender（唯一显式队列）
             |
       co_await AsyncSend
             |
             v
  FtRouteRegistry（唯一活动路由）
       |       |       |       |
       WS    Relay     RTC   UDP 模式的可靠 FT 旁路
```

### 5.1 新发送结果

```cpp
enum class FtSendStatus {
    kAccepted,
    kDisconnected,
    kCancelled,
    kTimedOut,
    kMessageTooLarge,
    kTransportError,
};

struct FtSendResult {
    FtSendStatus status = FtSendStatus::kTransportError;
    std::string transport_name;
    std::string detail;
    std::uint64_t sequence = 0;
};
```

`kAccepted` 只表示唯一传输层已经取得该消息的所有权。暂时拥塞不返回失败，而是
异步等待水位下降；连接关闭、取消或 deadline 到期才完成为错误。

### 5.2 Engine 两阶段提交

废弃 `SendFunc`、`Send()`、`FlushOutbox()` 和 Engine 内 `outbox_`。新接口：

```cpp
std::shared_ptr<const FtOutboundPacket> PrepareNextOutbound();
void CommitOutbound(FtPacketToken token);
void FailOutbound(FtPacketToken token, FtSendResult result);
```

同一 Session 同时最多有一个 prepared 数据块。发送成功后才提交文件游标和上传
进度。失败时不生成下一块，保留 `.download/.digest` 供续传。

### 5.3 唯一有界发送队列

建议初始值：

| 项目 | 初始值 |
| --- | ---: |
| 文件块载荷 | 120 KiB |
| 单 Session 队列上限 | 8 MiB |
| 高水位 | 6 MiB |
| 低水位 | 2 MiB |
| 控制消息预留 | 512 KiB |
| 正常拥塞诊断 deadline | 30 秒 |

容量按字节而不是消息数量统计。控制消息可以使用预留空间，但不能越过已经排队的
同 Session 数据造成协议乱序。

队列满时使用 channel/低水位通知挂起 coroutine，不允许 `DelayBySleep(1)`、
`DelayByCount(1)` 或无上限 PostTask。

### 5.4 单一路由

`FtRouteRegistry` 使用以下身份：

```text
device_id + stream_id + connection_instance_id + route_generation
```

Render 不再遍历所有 net plugin 发送同一 FT 消息。任务开始时绑定一个 route：

- 强制 RTC/Relay/Direct 时严格使用所选模式；
- 自动模式使用建连阶段最终选中的 route；
- 活跃任务中途不做 WS -> RTC 的无确认热切换；
- route 断开时当前任务暂停/失败并保留续传状态；
- 新 route 只供后续任务或明确的续传会话使用；
- 迟到的旧 generation 断开事件不能清理新 route。

UDP Direct 的视频/输入可走 UDP，文件继续使用同会话的可靠 WS/WSS 旁路，并在
统计中显示真实 FT transport，不能标成 UDP 文件传输。

### 5.5 Transport adapter

#### WS/WSS

- 完成条件是真正进入 asio2 发送队列；
- 队列空间释放时完成 writable wait；
- close/error 取消全部等待 operation；
- 不再由外层轮询 `GetQueuingMsgCount()`。

#### Relay

- 补充成功入队、目标 stream 不存在、断开和队列满的明确 completion；
- 不允许“SDK 不存在但返回 true”；
- 不允许只写日志后丢消息。

#### RTC/Direct RTC

- 不改 libwebrtc adapter 结构；
- GammaRay 包装层监听 `buffered_amount` 和低水位通知；
- DataChannel `Send()` 接受后才完成为 `kAccepted`；
- close/error 恢复所有等待 coroutine；
- callback 只用 weak owner，并 post 回 FT session strand。

### 5.6 块序号与最终完整性

第一阶段不依赖协议升级即可修复重复入队。稳定后增加 capability 协商：

- 启用现有 `FileTransferBlock.blk_id`，按 job/file 递增；
- 小于 expected 的块视为重复并丢弃；
- 大于 expected 的块视为缺失并终止/续传；
- 未声明 capability 的旧端保持兼容行为；
- 启用预留 `file_hash`，接收端在 `.download` 完成后计算 SHA-256；
- 校验成功后才 rename 和上报 100%；校验失败不得显示完成。

可靠通道下不默认增加逐块 ACK，避免高 RTT 下吞吐退化。路由断开使用文件级
续传；未来如需活跃路由热切换，再设计累计 ACK/滑动窗口，不在本阶段隐式加入。

## 6. 哪些回调应该改成 await

### 6.1 适合 await 的流程

- Console 认证、ticket 获取和连接授权；
- DNS/connect/TLS/WebSocket handshake；
- RTC offer/answer、ICE gathering 完成、DataChannel ready；
- 文件目录请求/响应、覆盖确认、发送、取消、续传和关闭；
- 配置更新、SetConfiguration、ICE restart；
- Service 请求/响应，如虚拟显示器增删；
- 插件 Start/Stop 内部的异步收敛；
- 有明确一次完成结果、错误和 deadline 的请求。

例如连接流程从多层 callback：

```text
get ticket callback
 -> connect callback
 -> auth callback
 -> create rtc callback
 -> channel ready callback
```

改成：

```cpp
auto ticket = co_await AcquireTicket(request);
auto transport = co_await ConnectTransport(ticket);
co_await Authenticate(transport, ticket);
auto session = co_await OpenRtcSession(transport);
co_await WaitFileChannelReady(session);
co_return session;
```

每一步都返回 typed result，并继承同一个 cancellation/deadline。

### 6.2 不应机械改成 await 的回调

- 音频 10 ms capture/playback callback；
- 视频帧、编码帧和光标连续流；
- 高频输入事件；
- Qt signal/slot 和 UI notification；
- 多订阅者广播事件；
- libwebrtc Observer/track/sink ABI；
- 插件 `GetInstance`/loader ABI。

这些回调应保持短小：只做校验、时间戳、持有安全 buffer、写入有界队列或 post
状态事件。后续耗时流程可由消费端 coroutine 处理。音视频不能进入通用
MessageNotifier 或等待可靠文件队列。

### 6.3 回调不会完全消失

操作系统、Qt、libwebrtc 和 asio2 都以 callback 提供部分完成事件。改造目标不是
让代码中零 callback，而是建立边界：

```text
第三方 callback（叶子 adapter）
  -> 完成一个 async operation / 写入有界 channel
  -> 业务 coroutine 被恢复
  -> 后续状态在线性流程中推进
```

禁止 callback A 注册 callback B、B 再更新对象 C 并由 C 猜测 A 是否失败。

## 7. 项目级迁移阶段

### Phase 0：冻结语义与复现

- 为当前重复入队、进度不动、RTC/WS queue 拥塞和多 route 派发建立确定性测试；
- 记录当前内存、队列长度、重复块数、线程阻塞时间；
- 不以“偶尔可以传完”作为基线通过。

### Phase 1：文件传输正确性热修

- 当前接口下先保证“已入队必返回 true，返回 false 必须完全未入队”；
- `PostFileTransferMessage` 返回明确结果，不再 `void`；
- 删除 2 秒后静默 drop；
- 修正删除确认框和批量远程删除结果聚合；
- 先恢复所有现有 transport 的可用性。

该阶段是可独立提交、可回滚的安全点。

### Phase 2：公共异步基础设施

- 从 MessageNotifier 抽取 `PxAsyncRuntime`；
- 实现 `PxAsyncScope`、取消、deadline、typed result 和 bounded channel；
- 增加 Qt executor adapter 和 callback-to-awaitable adapter；
- MessageNotifier 行为保持兼容并继续通过既有并发测试。

### Phase 3：FT Engine 与 Session

- 增加 `FtSessionState` 和 session strand；
- Engine 改 Prepare/Commit/Fail；
- 删除 Engine outbox 和同步 SendFunc；
- 磁盘工作进入 bounded worker；
- Client/Render 共用同一套 session driver。

### Phase 4：网络 adapter

按 WS/WSS、Relay、RTC、Direct RTC、UDP side-channel 顺序迁移。每个 adapter 完成
后立即跑单协议 10 轮，不等待全部完成。

### Phase 5：RouteRegistry

- 建立 stream/instance/generation 唯一路由；
- Render FT 不再广播给全部 net plugin；
- 验证 Windows 与 Web 同时连接、WS 与 RTC 同时存在、旧连接迟到退出。

### Phase 6：协议完整性

- capability（已完成）；
- `blk_id`（已完成）；
- SHA-256（已完成）；
- 清晰的错误与续传 UI（已完成）；
- Console 审计结束原因对齐实际结果（已完成）。

### Phase 7：推广 await 语义

仅在 FT 基础设施和卸载屏障稳定后，按风险和收益迁移：

1. 认证/连接/重连；
2. Service 请求响应；
3. RTC 信令和配置更新；
4. Panel/Console 有界请求流程；
5. 其他存在三层以上 callback 且具有单一完成结果的工作流。

每次迁移保留第三方边界 adapter，不进行全仓机械替换。

## 8. 测试和验收

### 8.1 AsyncRuntime/Scope

- concurrent Spawn、完成和取消；
- Scope 停止后拒绝新任务；
- timer/channel/socket 挂起时取消；
- operation completion 与 cancellation 同时发生；
- callback 已排队时 owner 销毁；
- coroutine 内请求 Stop；
- executor 线程内禁止同步等待自身；
- drain/cancel 两种关闭；
- 异常转换和未观察异常统计；
- 连续 construct/start/stop/destruct 10 轮。

### 8.2 插件 DLL 生命周期

- 插件有挂起 timer 时 Stop/Destroy；
- FT 正在等待低水位时关闭窗口；
- 网络 callback 已排队时卸载插件；
- RTC callback 在取消后迟到；
- Stop 从插件 coroutine 内触发；
- 宿主退出与插件退出并发；
- 实际 load/start/transfer/stop/unload 连续 10 轮；
- outstanding operation 未归零时证明宿主拒绝 FreeLibrary；
- EXE/DLL 混合版本被构建/发布门禁阻止。

### 8.3 FT 单元和组件测试

- busy 返回时零入队；accepted 时恰好入队一次；
- 1000 块无丢失、重复和乱序；
- 按字节容量的高低水位；
- 控制消息预留不破坏顺序；
- 发送等待时取消、断线、超时和 owner 销毁；
- WS+RTC 同时在线只走一个 route；
- route generation 隔离；
- duplicate/missing `blk_id`；
- SHA-256 不一致禁止完成；
- 进度单调且只在最终 rename 后到 100%；
- 限速与背压组合；
- 多任务公平性和取消隔离。

### 8.4 文件数据矩阵

- 0 B、1 B；
- 120 KiB-1、120 KiB、120 KiB+1；
- 64 MiB、1 GiB；
- 10000 个小文件；
- 空目录、多级目录、长路径、中文、空格和 Unicode；
- 已压缩/可压缩文件；
- 覆盖、跳过、应用到全部、取消和续传；
- 单文件、目录、批量上传、批量下载、批量删除和部分失败。

### 8.5 本机与 90 实机矩阵

- Windows Client -> 90 和 90 -> Windows Client；
- Web Client 支持的上传/下载路径；
- WS/WSS、Relay、标准 RTC、Direct RTC、UDP Direct + FT 可靠旁路；
- Windows 与 Web 同时连接；
- 文件传输期间画面、音频、输入、剪贴板和多显示器继续工作；
- 断网、退出、重连、连续连接、取消后立即重新传输；
- 每条关键路径连续 10 轮。

验收要求：

- 源/目标 SHA-256 一致；
- 无静默 drop；
- 无重复落盘；
- 无 1 ms busy wait；
- 单 Session 排队内存不超过配置上限加一个 prepared block；
- 任务失败显示 transport、阶段、原因和可否续传；
- 插件卸载后无 callback/coroutine 恢复；
- 无崩溃、死锁、卡死和持续内存增长。

## 9. 构建、发布与交付门禁

实现阶段每个批次必须：

1. 运行 `check_cpp_ownership`，新代码零裸指针、零异步裸 `this`；
2. 运行 AsyncRuntime、Scope、FT Engine、各 transport focused tests；
3. 关键 suite 连续 10 轮；
4. 使用 `build_cpp_*.bat` 或 `scripts/build_cpp_target.bat` 只构建本批受影响的
   CMake target；日常开发不得调用 `build_official.bat`；
5. 同批按需构建 Client EXE、SDK/RTC DLL 和所有受影响插件 DLL；
6. 将所有变化的 EXE、DLL、语言资源和 Web 资源同步到
   `build_official\dist`；
7. 比对 build tree 与 dist 的 SHA-256；
8. 部署到 90 后再次比对 SHA-256并执行实机矩阵；
9. 只有用户明确要求发版/整体编译时，才运行 `build_official.bat` 作为最终发布门禁。

只编译通过、只替换 EXE、只完成 Engine 单测或只在一种 transport 上传一个小文件，
都不构成交付完成。

## 10. 明确不做的事情

- 不全面引入 asio3；
- 不把所有回调机械替换成 coroutine；
- 不让音视频帧经过 MessageNotifier 或 FT queue；
- 不修改 libwebrtc 内部裸指针/Observer ABI；
- 不修改插件 `GetInstance`、loader handle、实例身份和卸载合同；
- 不跨 DLL 暴露 `asio::awaitable`；
- 不使用 detached coroutine；
- 不在活跃传输中无确认热切换 transport；
- 不通过扩大队列或延长 sleep 掩盖背压问题。

## 11. 完成定义

只有在以下条件全部满足后，文件传输 awaitable 改造才完成：

- Engine 不再使用二义性的 `bool SendFunc` 和内部 outbox；
- 全链路只有一个显式、按字节有界的 Session 发送队列；
- 每个 Session 只有一个活动 FT route；
- WS/WSS、Relay、RTC、Direct RTC 和 UDP 可靠旁路均实现真实 completion；
- 取消、超时、断线和插件卸载可以恢复并收敛所有挂起 operation；
- 插件卸载屏障通过真实 DLL 生命周期测试；
- touched scope 满足智能指针标准；
- 文件完整性、进度、续传和错误 UI 通过测试；
- 本机和 90 的关键矩阵连续 10 轮通过；
- 日常批次的受影响 C++ target 编译成功，dist 和 90 的运行产物 hash 与构建产物一致；
- 用户明确进入发版阶段后，`build_official` 最终整编成功。

项目级 callback-to-await 迁移是后续分批工作，不以“全仓无 callback”为完成目标；
其完成标准是所有适合结构化等待的业务流程具备统一 executor、取消、deadline、
typed result 和生命周期域，第三方/实时边界回调保持短小且不再承载业务状态机。

## 12. 2026-08-28 实施与验收记录

### 12.1 本批已落地

- 增加 `FileTransferSendResult`，区分 accepted、busy、disconnected 和
  transport error；Client SDK、RTC、Relay、WS 和 FT 壳不再用一个 `bool`/`void`
  混合表达所有发送结果。
- `FtEngine` 增加 `PrepareOutbound`、`CommitOutbound`、`RetryOutbound` 两阶段发送；
  busy/断线时 prepared 消息仍由 Engine 持有，只有目标 transport 接受后才提交。
- 增加 `PxAsyncRuntime`、`PxAsyncScope` 和 `FtAsyncSession`，覆盖取消、停止收敛、
  callback 已排队时 owner 销毁、回调内停止和连续启停。`MessageNotifier` 已复用公共
  runtime；Render 和 Windows Client 的生产 FT 壳均已切换到 `FtAsyncSession`。
- `FtAsyncSession` 支持注入共享 Runtime 和既有 Engine。Render 使用一个共享 Runtime、
  每个 stream 一个 state-strand Session；停止一个 Session 不会停止其他连接或共享
  Runtime。Windows Client 使用单 Session，Qt 回调捕获 `QPointer`，对象销毁后立即失效。
- Render 的旧 condition-variable worker、任务 deque 和 1 ms Tick 冲刷已删除；Client 的
  同类 worker 也已删除。停止/断线先在 Session strand 执行取消或
  `DisconnectCleanup`，随后等待 coroutine 收敛，再允许插件进入 `OnDestroy`。
- 增加 `FileTransferRouteRegistry`，Render 每个 stream 只绑定最近一次真实入站 FT
  transport；旧 route 的迟到断开不能删除新 route。
- route 身份贯穿 `plugin_id + stream_id + connection_instance_id`。定向回包接口现将
  connection instance 传给 net plugin；Local RTC 精确查找 map 中的连接，标准 RTC
  使用每个 server 实例独立 UUID。相同 stream 的新旧 RTC 短暂共存时，回包不会再
  被旧实例吞掉。
- Render FT 使用独立入站消息队列和断开队列，磁盘/协议处理不在网络分发线程执行；
  出站使用唯一 route 和两阶段提交。
- Web 上传在发送 digest 前先安装确认 waiter，并为 confirm 增加 30 秒 deadline、
  cancel/error/close 清理；修复快速同步回包造成的 lost wake-up。HTTP Render 页面
  的 SHA-256 使用 CryptoJS fallback，真实验收仍要求 64 位十六进制摘要。
- 删除确认框、错误传播、Console 审计闭环和相关语言资源随同本批交付。

### 12.2 本批定位并关闭的实机竞态

原失败表现是第二次连续标准 RTC 上传停在 0 字节，30 秒后提示等待远端文件确认
超时。90 端日志证明新连接已经收到目录请求，旧连接的迟到断开也已被 generation
隔离，但 `send_confirm` 的发送接口仍只按 `plugin + stream_id` 遍历 RTC server。
旧、新 server 同 stream 共存约 5 秒时，回包可能被已经 ICE disconnected、尚未
sweep 的旧 server 接受。

修复后，FT 入站 route 保存的 `connection_instance_id` 会一直传到具体 net plugin；
RTC 回包只允许交给完全匹配的 server 实例。旧实例即使仍在 map 中也不会收到新作业
的确认、块或完成消息。

### 12.3 自动化与实机结果

| 门禁 | 结果 |
| --- | --- |
| `check_cpp_ownership` | PASS；新增代码无裸指针、手工 ownership 或异步裸 `this` |
| Native focused tests | 12 个测试程序、105 个用例/轮，连续 10 轮，共 1050 次 PASS |
| Web tests | voice 19 assertions + Vitest 15 tests/轮，连续 10 轮 PASS |
| Web lost-wakeup regression | 同步 `RTCDataChannel.send()` 重入 confirm 场景 PASS |
| 标准 RTC 真实 FT | 本机 Web -> 90，上传+下载+SHA-256，连续 10/10 PASS |
| 同轮功能共存 | 每轮同时验证画面、系统音频轨、输入通道、虚拟屏新增、RTC 重建、切屏 |
| 全量 CMake/build_official | PASS；受影响 C++、Rust client、Parsec VDD 和 dist 收集完成 |
| `build_official\dist` | build tree 与 dist 受影响运行产物 SHA-256 一致 |
| 90 部署 | service/render 启动、20371 可达，部署产物与 dist SHA-256 一致 |

真实 10 轮每轮传输 50 字节独立内容，上传返回的 SHA-256 与重新下载内容的 SHA-256
严格相等；每轮使用新 RTC 连接，轮间仅等待 100–150 ms，覆盖旧连接尚未完成 sweep
的重叠窗口。临时 Console 用户在测试结束后删除。

### 12.4 已发现但不冒充本批通过的事项

- 在开启“剪贴板回执也必须成功”的综合回归中，前 4 轮通过，第 5 轮剪贴板回执
  10 秒未到；该轮尚未进入文件步骤，因此不计为 FT 失败，也不能写成综合功能
  10/10。FT 专项随后独立完成 10/10。剪贴板控制通道竞态需单独跟踪。
- WS、Relay、UDP Direct 可靠旁路、标准 RTC 和 Direct RTC 的真实大文件 10 轮矩阵
  已在 12.9 完成。WSS 不属于当前产品运行拓扑：按既定部署决策只有 Console 使用
  HTTPS，Render 继续提供 HTTP/WS，不能把未启用的 WSS 写成实机通过。
- Windows 原生 FT UI 的双向实机操作、64 MiB/1 GiB、10000 小文件、全覆盖/跳过/
  取消/断点续传矩阵仍需执行。
- `blk_id` capability 和接收端最终文件 SHA-256 门禁已在 12.11 完成；插件真实
  load/transfer/stop/unload 外部加载器矩阵仍需补齐，尚未达到本文第 11 节的最终
  完成定义。

### 12.5 生产 Session 接入续验

- `FtAsyncSession` 单测增至 7 个场景，新增共享 Runtime 多 Session 隔离、空依赖拒绝、
  finalizer 完成后停止和命令异常传播。
- 7 个 FT 原生测试程序连续执行 10 轮，共 70 次程序执行全部通过。
- `build_official` 全量构建通过，Render/Client FT 插件 build tree 与 dist SHA-256
  一致；dist 的 410 个文件与 90 安装目录逐文件 SHA-256 一致。
- 最终 90 部署版本完成标准 RTC 真实连续 10 轮。Console 持久化审计独立确认
  10 次上传、10 次下载，共 20/20 条记录均为 `succeeded/completed`，失败 0；
  每轮还覆盖画面、系统音频轨、输入、剪贴板发送、虚拟屏新增、RTC 重建、切屏和删除。
- 90 的 service/render 和 20371 正常，最近日志未命中 crash、FT timeout、route not
  found 或 Session 停止超时；临时 Console 用户已自动删除。

### 12.6 检查点后的 Phase 4 进展

- 检查点 `8a8e434ee` 之后，Render 的 net plugin FT 接口由 `bool` 改为
  `FileTransferSendResult`。RTC、Direct RTC、WS/WSS 和 Relay 现在分别返回
  accepted、busy、disconnected 或 transport error，不再由路由层把所有失败压成
  “selected route busy”。
- 标准 RTC 增加只读的 FT DataChannel connected 查询；未找到指定
  `connection_instance_id`、通道已关闭和发送队列拥塞现在可以分开诊断。
- WS/WSS 的 accepted 定义收紧为目标 FT router 存在、session 已启动且消息已经提交给
  asio2 `async_send`；Relay 和两种 RTC 使用统一的 256 消息高水位常量。
- Client SDK 与 Render 共用 `kMaxFileTransferQueuedMessages`，避免两端水位边界漂移。
- 该检查点后的第一步是 Phase 4 的同步 preflight/入队结果层；后续低水位等待的实现与
  验收见 12.7。单协议真实大文件背压矩阵未完成前，仍不把整个 Phase 4 标记为完成。

### 12.7 可写低水位等待、关闭取消与 3.3.62 验收

- `FileTransferWritableSignal` 提供跨线程、一次完成的 `writable/closed` 通知。它不跨 DLL
  暴露 `asio::awaitable`；Transport 只发布通知，`FtAsyncSession` 在自己的 executor 上
  用 coroutine 等待并恢复。
- `FtAsyncSession` 收到带可写信号的 `busy` 后不再执行 2 ms 重试轮询。队列到达统一的
  1/4 低水位后恢复；Session stop、Transport close 或 error 会取消挂起 timer，迟到的
  Transport completion 只持有 weak timer，不会访问已经销毁的 Session。
- Render WS/WSS 和 Relay 使用 asio2/Relay WS 的真实发送完成回调触发低水位；标准 RTC
  和 Direct RTC 在 GammaRay DataChannel 包装层的 `OnBufferedAmountChange` 中检查
  `buffered_amount`，不修改 libwebrtc observer ABI 和其既有裸指针合同。
- Windows Client 的 WS/WSS 与 Relay 同样由真实发送完成触发。Client RTC/Direct RTC
  遵守“不改 `px_webrtc_client` 结构”的约束，暂由既有 16 ms RTC 驱动读取包装层水位并
  发布信号；已经消除 FT Session 的 2 ms busy polling，但该路径仍需在后续独立接口评审
  中决定是否增加不破坏 ABI 的原生低水位通知。
- 所有通道在返回 `busy` 后再次检查当前水位，覆盖“检测到满 -> 建立 waiter”之间队列已
  经排空的 lost-wakeup 窗口。Relay 底层本批改动的异步裸 `this` 捕获已迁移为
  `weak_ptr + lock()`。

本批自动化与部署结果：

| 门禁 | 结果 |
| --- | --- |
| `check_cpp_ownership` / `git diff --check` | PASS |
| Low-water 单测 | 通知、关闭、迟订阅、重复通知、无轮询等待、stop 取消、owner 销毁后迟到唤醒均 PASS |
| Native focused tests | 10 个相关测试程序连续 10 轮全部 PASS |
| 增量编译 | Client SDK、WS、Relay、标准 RTC、Direct RTC 全部 PASS |
| `build_official` | 3.3.62 全量 C++、Rust Client、Parsec VDD、Web Client、Console 和三项 Rust Server PASS |
| `build_official\dist` | 9 个关键运行产物逐项 SHA-256 一致；Web Client 5/5、Console 4/4 文件一致 |
| 90 部署 | 9 个关键运行产物及两个 Web 目录与 dist 一致；service Running，20371 可达 |
| 标准 RTC 稳定性 | 本机到 90 连续 10/10，新连接、1920x1080 画面推进、无冻结/丢帧统计 |
| 标准 RTC 大文件背压 | 16 MiB 不可压缩内容上传+下载+SHA-256 连续 10/10；每轮均真实触发 busy/writable wait |

大文件验收使用浏览器内确定性伪随机 ASCII，避免重复字符串被压缩后无法施压。每轮原始
16 MiB、实际上传约 13.4 MiB，上传完成后立即反向下载并严格比较 SHA-256，最后删除远端
文件；10 轮结束后 90 的测试临时文件为 0。90 日志中每轮 `accepted=144`，10/10 的
`busy` 和 `writable_waits` 都大于 0，`disconnected=0`、`transport_errors=0`。其中一轮
底层漏发两次低水位通知，1 秒安全 deadline 成功恢复，最终仍完成且摘要一致。

在加入安全 deadline 前，同一不可压缩 64 MiB 用例曾在下载约 5 分钟后超时；远端上传
文件已经完整写入，Session 统计显示 118 次 busy、111 次唤醒和若干未完成等待。将正常
事件驱动保留、仅把异常兜底由 30 秒收紧到 1 秒后，64 MiB 双向传输通过，日志为
`busy=193`、`writable_waits=193`、`writable_wakeups=181`、`writable_timeouts=0`、
`disconnected=0`，上传/下载 SHA-256 一致，失败用例残留也已清理。

标准 RTC 和 Direct RTC high-water 已关闭；仍需用同一脚本矩阵覆盖 WS/WSS、Relay，
并补 Windows 原生 FT UI、断线取消/续传、插件真实 unload 和更大文件后，才能关闭整个
Phase 4。

### 12.8 Direct RTC 真实背压与连续会话验收

- `run_rtc_lan_case.ps1` 和 `run_rtc_lan_stability.ps1` 增加显式
  `ConnectionMode=rtc|rtc_direct`，同一套门禁可验证标准 RTC 与 Direct RTC，避免仅靠
  Render 日志推断实际连接方式。
- CDP 连接轮询改用 30 秒墙钟总预算、单次最多 1 秒；原实现的 60 次轮询每次可等待
  10 秒，页面线程无响应时会错误拖到约 600 秒。FT ready/job 查询同样按剩余 deadline
  限制，并输出 waiting-ready、upload、download 阶段，超时现场可以准确归类。
- Direct RTC 的诊断 Chrome 在每轮结束时由 `taskkill` 强制终止，模拟客户端异常退出。
  Render 需要约 4 秒检测 ICE disconnected，再等待 5 秒宽限并 sweep；若立即开始下一轮，
  新请求会按设计返回 occupied。稳定性脚本新增可配置轮间等待，本验收使用 12 秒覆盖完整
  回收窗口。7 秒不足的探索轮稳定复现 occupied，不计入通过轮数。
- 最终本机 Web 到 90 的 Direct RTC 连续 10/10 PASS，总计 6.2 分钟。每轮新建 host UDP
  RTC 会话，1920x1080 画面继续解码、观测窗口新增丢包为 0；每轮上传 16 MiB 确定性
  伪随机内容，再下载到内存并校验 SHA-256
  `ab6c06393858b578167fccf7d99f667e87836f63540372090d5adb796819aa2e`，最后删除远端文件。
- 第 10 轮后 `cdpChrome=0`；临时 Console 用户、Session、Ticket 均为 0。中断探索轮留下
  的远端测试文件已显式删除，最终没有已知测试残留。

### 12.9 WS、Relay、UDP Direct 与最终发布包回归

本批增加原生 `test_ft_transport_e2e` 和 `run_ft_transport_e2e.ps1`。测试直接使用
Windows Client 的 `NetClient + FtAsyncSession + FtEngine` 生产链路，生成确定性不可压缩
文件，依次执行上传、下载、SHA-256 比较和远端删除；每轮使用新 Ticket 和 stream，结束
后删除临时 Console 用户、Session 和 Ticket。支持 `ws`、`wss`、`relay` 和
`udp_direct` 选择，其中 WSS 仅保留代码能力，不在当前 Render HTTP/WS 部署中执行。

实机回归中定位并修复了以下仅靠单元测试无法发现的问题：

- WS/WSS `async_send` 的 buffer 生命周期曾依赖函数实参求值顺序；现在先保存共享 payload，
  再把稳定地址交给异步发送，completion 前不会释放。
- Render 的通用 FT 分发错误依赖“媒体客户端数量”，导致独立文件连接虽然在线仍被拒绝；
  现以所选 FT transport 自身的连接和队列状态为准。
- Relay 的媒体 `paused_stream` 曾错误阻塞独立 FT 房间；媒体暂停不再影响可靠文件通道。
- WS close、disconnect 和 Relay room destroyed 现在都会按 connection instance 收敛
  `FtAsyncSession`，断开后立即输出统计，不再遗留活跃 Session。
- Relay server 会复用同一对设备的 room id，且新房间首个 target message 可能早于
  `RoomPrepared`。旧实现把序号和 route identity 跨房间复用，连续第 2 轮可复现
  `current=0,last=286` 后停滞。现在为每个房间代际分配唯一 connection instance，
  序号按代际保存，并允许首包先建立 provisional route；修复后连续 10 轮通过。
- UDP Direct 的文件只运行模式此前仍创建媒体/裸 UDP 通道，FT URL 也没有附加 Ticket。
  现在 file-transfer-only 只建立 WS/WSS FT 可靠旁路并携带 Ticket；正常 UDP Direct
  模式仍保持 WS 控制/文件面与裸 UDP 媒体面的双通道结构。

最终 90 实机结果：

| 路径 | 测试内容 | 结果 |
| --- | --- | --- |
| WS | 每轮 16 MiB 上传、下载、SHA-256、删除 | 10/10 PASS |
| Relay | 每轮新建/销毁 FT room，16 MiB 双向校验和删除 | 10/10 PASS |
| UDP Direct FT 可靠旁路 | 原生 `kUdpDirect` file-transfer-only，16 MiB 双向校验和删除 | 10/10 PASS |
| 标准 RTC | 1920x1080 画面推进 + 16 MiB 双向 FT | 10/10 PASS，丢包/冻结 0 |
| Direct RTC | 独立 host UDP 会话 + 画面 + 16 MiB 双向 FT | 10/10 PASS，丢包/冻结 0 |
| Native 生命周期门禁 | 8 个相关测试程序，每程序 `gtest_repeat=10` | 全部 PASS |

所有 16 MiB 用例的 SHA-256 均为
`ab6c06393858b578167fccf7d99f667e87836f63540372090d5adb796819aa2e`；WS、Relay 和
UDP Direct 每轮接受 144 条 FT 消息，无 disconnected 或 transport error。RTC 两组
结束后 `cdpChrome=0`；各脚本最终报告临时用户、Session、Ticket 为 0，远端测试文件已
由用例删除。

发布门禁方面，Visual Studio 18 official 工具链完成全量 C++、Rust、Parsec VDD 与
dist 收集。26 个受影响 C++ 运行产物的 build tree/dist SHA-256 一致；最终完整 dist
共 521 个文件部署到 90 后逐文件比对，missing=0、mismatch=0，`px_service` Running，
20371 可达。Relay 与 UDP Direct 最后修复已经重新执行 official 收集、部署和同样的
哈希门禁，最终一次构建输出即为交付基准。

仍未关闭的扩展验收不应与本批“异步发送与真实 completion”混为一谈：Windows 原生 FT
UI 的人工双向操作、64 MiB/1 GiB、10000 小文件、覆盖/跳过/取消/断点续传、真实 DLL
load/transfer/stop/unload，以及公网跨网 TURN UDP/TCP 场景仍需单独执行。当前没有公网
环境，不能把跨网 TURN 项写成已通过。

### 12.10 第一批通用 await 基础设施与按需编译门禁

本批把后续消除回调链需要的通用能力落到 `px_common_new`，并先迁移 Render 与
`px_service` 之间两个边界清晰、可独立验证的请求：连接 Ticket 兑换和虚拟显示器操作。

- 新增稳定的 `PxResult<T>`/`PxAsyncError`，错误包含固定错误码、阶段、可读信息、
  可重试标志和可选业务明细码，不再依赖模糊字符串判断状态。
- 新增一次完成的 `PxAsyncOneShot<T>` 和按 request id 管理的
  `PxAsyncRequestRegistry<T>`。完成、超时、取消、断连和服务退出竞争时只有一个终态；
  重复 request id 会在入队前拒绝，连接断开会一次性结束所有未完成请求。
- coroutine frame 只持有 `shared_ptr`；等待入口为静态 coroutine，不隐含保存裸
  `this`。兼容旧调用方的 callback 包装使用 `weak_ptr + lock()`，新旧接口共用同一状态机。
- `RdContext` 统一拥有进程级 `PxAsyncRuntime`，`MessageNotifier` 使用外部 runtime 时不再
  越权停止它，避免每个模块各建线程池及析构顺序互相影响。
- Render Service 发送前完成参数、连接状态和队列容量检查；协议解析失败、服务停止、
  disconnect、deadline 和业务拒绝均返回可区分错误，并保证 registry 清理。

本批同时新增仓库根目录的 C++ 增量入口：Render、Windows Client、Panel、公共库、SDK、
单个 Render 插件、全部 Render 插件和任意测试目标都可以单独构建。统一底层脚本只调用
现有 Ninja 构建树中的明确 CMake target，不递增版本、不运行 npm、不调用 Cargo、不重建
Web，也不收集整个 dist。需要运行的 EXE/DLL 仅按组件发布到 `build_official\\dist`，随后
逐文件比较 SHA-256。`build_official.bat` 明确保留为用户要求发版时才执行的全量门禁。

本地验收结果：

| 门禁 | 结果 |
| --- | --- |
| `build_cpp_render.bat` | `px_render` 增量编译和 dist 发布 PASS |
| `build_cpp_tests.bat` | 3 个相关测试目标增量编译 PASS |
| 异步运行时 | 11 tests × 10 轮 PASS |
| MessageNotifier | 32 tests × 10 轮 PASS（错误日志为异常/背压预期用例） |
| Render Service RPC 状态机 | 3 tests × 10 轮 PASS |
| C++ ownership / diff whitespace | PASS |
| 构建脚本 target 校验 | 46/46 个声明目标存在 |
| `px_render.exe` build/dist/90 SHA-256 | 一致：`F137C87DAD45702178253BAB1A86D1502C0487DC513D564F89452275E440A0C5` |
| 90 原生账号 Ticket + Direct RTC | 10/10 PASS；画面、音频、文件通道均就绪 |
| 90 WebClient 虚拟显示器 | 最终修正版连续 10/10 PASS；新增、发现、切换、持续解码、切回、删除和单屏恢复 |
| 测试数据清理 | 每轮临时 user/session/ticket 均为 0，最终虚拟屏 owned=0 |

90 的第一阶段探索回归发现：同一毫秒内的两次拓扑通知会生成相同查询 id；同时，删除屏幕
引发状态 lane 重建时，单靠消息总线可能丢掉最后一次 owned 状态。最终实现用原子序号和
pending gate 合并并发 refresh，用 callback 作为拓扑重建兜底，再按
`topology_generation + owned_display_count` 幂等应用。最终 10 轮日志中 20 个客户端新增/
删除请求均有 20 个 Service 结果，`REQUEST_IN_PROGRESS`、超时、断连和 pending 泄漏为 0。

最终验收没有使用 `build_official.bat`。实现过程中曾误触发该入口，发现后立即终止；它造成
的临时版本号改动已逐项恢复，被清空的 Web/Console dist 目录也已从现有构建产物恢复，且
其输出不计入本批验收。本文结果全部来自 `build_cpp_*.bat` 的局部 C++ 构建。这里不把
“局部 C++ 与实机功能通过”写成“发版整体验收通过”；公网 TURN 仍受环境限制。

### 12.11 Phase 6 capability、块序号与最终 SHA-256 门禁

协议完整性核心已完成，采用向后兼容的按位能力协商：

- `FileTransferDigest.capabilities` bit 0 表示严格块序号，bit 1 表示最终 SHA-256；未知位
  必须忽略，未声明 capability 的旧端继续按原协议工作。
- 新端的 `FileTransferBlock.blk_id` 从 1 开始，按文件独立递增，EOF 空块也占用序号；
  接收端在写盘前拒绝重复、缺失和乱序块。
- SHA-256 对解压后的原始文件内容增量计算，只放在每个文件的 EOF 空块中。接收端只有在
  哈希一致后才允许把 `.download` rename 为正式文件；缺失或不一致时保留
  `.download/.digest`，不得报告完成。
- 断点续传双方都会先读取已存在的前缀并注入增量 SHA 状态，再从续传偏移处理剩余内容，
  因而最终摘要覆盖完整文件而非仅覆盖后缀。
- SHA-256 使用项目内纯 C++ 实现，不增加 OpenSSL 运行依赖；已用空串和 `abc` 的 RFC
  已知向量校验。

专项自动化新增 9 个完整性用例，覆盖能力声明、发送端序号/EOF hash、1000 个严格有序块、
重复/缺失/乱序拒绝、损坏摘要保留临时文件、缺少 EOF 摘要、旧端兼容、多文件逐文件重置，
以及断点续传前缀参与哈希。最终 8 个 FT 原生测试程序连续 10 轮，共 780 个 test-case
执行全部 PASS；ownership 门禁和 `git diff --check` 同步通过。

90 实机验收结果：

| 路径 | 内容 | 结果 |
| --- | --- | --- |
| WS | 每轮 1 MiB 上传、下载、SHA-256、删除 | 10/10 PASS |
| Relay | 每轮 1 MiB 上传、下载、SHA-256、删除 | 10/10 PASS |
| UDP Direct 可靠 FT 控制通道 | 每轮 1 MiB 上传、下载、SHA-256、删除 | 10/10 PASS |
| 标准 RTC | 1 MiB 双向 FT，同时验证 1920×1080 画面、host UDP、RTT | PASS |
| Direct RTC 最终发布冒烟 | 1 MiB 双向 FT、SHA-256、35 帧增量、无丢帧/冻结 | PASS |

上述 1 MiB 内容摘要均为
`c87f2edbd8e31bf4d83d444691dbb2cf7cc0185720e8e0a2badc6ff0f8f1abe0`。
测试结束后临时 Console user/session/ticket 为 0，远端测试文件已删除；`px_service` 为
Running，20371 可达。

本批只执行 C++ 按需构建，没有调用 `build_official.bat`。最终 FT 运行产物已发布到
`build_official\dist` 并部署到 90，build tree/dist/90 三方 SHA-256 一致：

| 产物 | SHA-256 |
| --- | --- |
| `deps\rd_plugins\ft.dll` | `5AFD5C1FA2690FC2AF6DCF5CE354385645A66D9894FB6C85E882BF6DBAD419F6` |
| `deps\ct_plugins\ft.dll` | `D9CF963C1BE6F8ADCF38814EB4F2098C0533930D74E39AB8EF07C62F9B7C4ADC` |

部署探索还确认了一项必须执行的兼容性门禁：protobuf 生成类型会跨 Render 和网络插件 DLL
边界传递；修改 `.proto` 后，不允许只替换 FT DLL。`px_render.exe`、FT 插件及所有消费该
protobuf 对象布局的网络插件必须由同一生成版本构建并原子发布。混用新旧 DLL 会导致消息
已到 FT 插件但确认无法返回。90 已同步部署 `px_render.exe`、Client/Render FT、
`net_ws`、`net_relay`、`net_rtc`、`net_rtc_local` 和 `net_udp` 后完成上述实机矩阵；以后
发布脚本必须把这组产物视为同一协议兼容单元。仓库新增
`build_cpp_ft_protocol.bat` 作为唯一的 FT `.proto` 变更按需入口：它同时覆盖 Panel 的
Cp/Rp 审计消息消费者，只构建上述 C++ 目标，不运行 npm/Cargo、不递增版本，也不触发
`build_official.bat`，随后整组发布到 dist 并逐项执行 SHA-256 门禁。

### 12.12 Phase 6 结构化终态、Windows 提示与 Console 审计

文件传输结束不再只有一个 `success` 布尔值。`ClassifyTerminal` 把 Engine 实际错误
统一归一为稳定的 `status + end_reason + resumable`：

- status 为 `succeeded / failed / cancelled / skipped / aborted`；
- reason 覆盖完成、用户取消/跳过、会话中断、SHA-256/块序号异常、权限、
  文件数限制、源文件不存在、目标占用、I/O、超时、断线和路由/传输通道错误；
- Cp/Rp 结束消息增加 `status` 和 `end_reason`，空字段仍按旧端 `success` 兼容；
- Client/Render 从 FT Engine 生成终态，Panel 落库并推送 Console，Console Server 校验
  status/success 一致性，Web 展示本地化状态和结束原因；
- Windows FT 队列不再直接展示底层英文错误，改为本地化原因，对可恢复错误
  明确标注“可续传”。

专项门禁结果：

| 门禁 | 结果 |
| --- | --- |
| 9 个 FT C++ 测试程序 | 83 tests × 10 轮，共 830 次，全部 PASS |
| 终态/新旧 Cp/Rp 兼容 | 5 tests × 10 轮 PASS |
| Console Rust 终态一致性 | PASS，包含 cancelled/skipped 成功值冲突拒绝 |
| Console Web | `vue-tsc` PASS；终态显示 3 tests PASS |
| 真实标准 RTC | 10/10 PASS；每轮 1 MiB 双向 FT、SHA-256、1920×1080 画面与 RTC 统计 |
| Console 实际审计 | 20/20 条 In/Out 记录均为 `succeeded + completed` |
| 真实取消链 | 10/10 PASS；WebClient 取消 64 MiB 作业，审计 10/10 为 `cancelled + user_cancelled` |
| 资源清理 | 临时 user/session/ticket 均为 0，CDP Chrome 为 0，90 无取消文件或临时文件残留 |

原稳定性脚本默认没有设置 FT 字节数，过去会输出 `fileTransfer:null`，不能当作
文件传输验收。`run_rtc_lan_stability.ps1` 现增加显式 `-FtE2eBytes`；本批使用
`1048576`，10 轮下载内容的 SHA-256 均为
`c87f2edbd8e31bf4d83d444691dbb2cf7cc0185720e8e0a2badc6ff0f8f1abe0`。

本批只使用 FT C++、Console Web 和 Console Rust 的独立按需入口，没有运行
`build_official.bat`。C++ 协议兼容单元和三份语言资源已发布到
`build_official\dist`；同一批 14 个 C++ 运行文件部署到 90 后逐文件 SHA-256 全部一致。
为验证取消终态，WebClient 调试 API 增加受控的 pattern upload/cancel 入口，WebClient
生产包 5 个文件也已完成 source/dist/90 逐文件 SHA-256 门禁；
`px_service` 为 Running，20371 可达。

### 12.13 Phase 6 数据规模、边界、冲突和断点续传验收

在结构化终态与完整性门禁基础上，本批继续使用生产 FT 协议和 90 实机补齐数据矩阵。
所有内容均在接收端重新下载并计算 SHA-256，不能以“进度到 100%”代替内容正确；每个
成功用例最后删除正式文件，并检查 `.download/.digest` 临时文件和测试账号、Session、
Ticket 是否残留。

| 场景 | 链路与内容 | 结果 |
| --- | --- | --- |
| 大文件 | 原生 WS，1 GiB 上传、下载、SHA-256、删除 | PASS；234.3 秒，摘要 `d83ae5d31fa85b557f229e13c07ef55fb5698ecd943df34b856588f8100d8f8c` |
| 中等文件连续回归 | 原生 WS，64 MiB 双向校验和删除 | 10/10 PASS；摘要 `82695bf4430b56c0035444eace35b5abd10f89d524a5455a1247b781ae0e3b0e` |
| 其他可靠链路 | Relay、UDP Direct 的可靠 FT 旁路，64 MiB 双向校验和删除 | 各 1/1 PASS；摘要同上 |
| 文件大小边界 | 0、1、122879、122880、122881 字节 | 每个大小 10/10 PASS，共 50 轮；覆盖空文件、块边界前后 |
| 大量小文件 | 10000 个文件、108 个目录，含 Unicode、空格、嵌套和空目录 | PASS；约 8.3 分钟，目录清单摘要 `544ffabfac210f11d512adc29f9aba2c8a527a3264325f2d23d590f88684189c` |
| 覆盖冲突 | 1 MiB 已存在目标选择 overwrite | 10/10 PASS；最终内容为新摘要 `3d43395ab3efbb331714fb652ca3376b6ff4f7b53a5b9a36c1ef9a6e448d8d7e` |
| 跳过冲突 | 1 MiB 已存在目标选择 skip | 10/10 PASS；目标保持原摘要 `c87f2edbd8e31bf4d83d444691dbb2cf7cc0185720e8e0a2badc6ff0f8f1abe0` |
| Windows 长路径 | 100 个文件、18 个目录，包含真实超过 260 字符的路径 | PASS；摘要 `0b1913996b1c21bb020858124687132c64f6b8a0046381dc9dec157109887d18` |
| RTC 断点续传 | WebClient 标准 RTC，50 MiB 在 20 MiB 主动断开，重建 RTC 后续传、回读、删除 | 10/10 PASS；每轮恢复 offset 为 20971520，摘要 `8e8e32c397d40862...`，临时文件 0 |

数据矩阵定位并修复了四项边界问题：

- Console Ticket 有效期为 30 秒。10000 文件用例原先在建连前生成并哈希全部输入，导致
  Ticket 在真正兑换前过期；测试和生产流程现在都要求先完成授权/建连，再准备耗时数据。
- Render 的 10000 文件限制原先把目录也计入总数，导致“恰好 10000 个文件加若干目录”
  被错误拒绝。`CountRecursiveRegularFiles` 现在只统计可传输的普通文件，目录不消耗文件
  配额，路径安全测试覆盖了该语义。
- Windows 长路径现在在文件系统边界统一转换为 `\\?\` 或 `\\?\UNC\` 扩展路径，
  同时先执行 `make_preferred()`，避免扩展路径前缀与 `/` 混用；对外协议仍返回普通 UTF-8
  路径，不泄漏 Windows 内部前缀。
- 原生目录测试的清理曾只调用 `RemoveDir`，非空目录不会被删除。测试现在与产品行为一致：
  先遍历删除普通文件，再自底向上删除空目录；10000 文件和长路径用例最终均无已知残留。

最终自动化门禁：

| 门禁 | 结果 |
| --- | --- |
| 9 个 FT C++ 测试程序 | 85 tests × 10 轮，共 850 次，全部 PASS |
| 路径安全专项 | 22 tests × 10 轮，包含超 260 字符路径，全部 PASS |
| Console Web 终态测试 | 3 tests PASS；`vue-tsc --build` PASS |
| C++ ownership | 无新增裸指针、手工所有权或 `[this]` 捕获，PASS |
| `git diff --check` | PASS |
| 90 服务状态 | `px_service` Running，TCP 20371 可达，`resume50_*` 残留 0 |

本批只运行 `build_cpp_ft_protocol.bat` 和明确测试目标，没有调用 `build_official.bat`。
最终 FT DLL 的 build tree、`build_official\dist` 和 90 安装目录三方 SHA-256 一致：

| 产物 | SHA-256 |
| --- | --- |
| `deps\rd_plugins\ft.dll` | `E5C0F5F7F7F2ABAAC662BBBCD5FF64BBD88AF0C2CE95823E0DF67916511B76D3` |
| `deps\ct_plugins\ft.dll` | `E0473BAEA45028D009ACD8D52D2129E6555769369E183CAEFA9F7D3575271F84` |

当前 Render 实机部署为 HTTP/WS，因此 WSS 只保留代码能力，不伪造实机通过结果；公网
跨网 TURN UDP/TCP 也仍受环境限制。本节验收结论只覆盖当前可用的本机到 90 内网链路。

### 12.14 真实 FT DLL 卸载屏障与共享插件上下文修复

新增 `test_ft_plugin_dll_lifecycle`，不改变 `GetInstance` 或插件实例 ABI。每轮执行真实
`LoadLibrary(ft.dll)`，创建插件并连续投递 64 个目录请求，使 Session/插件工作线程存在
排队回调；随后调用既有 `OnStop`、`OnDestroy`，释放动态库，并用
`GetModuleHandleW(L"ft.dll") == nullptr` 证明模块确实卸载，而不是只完成对象级单测。

首轮门禁稳定复现 `0xC0000409 / FAST_FAIL_FATAL_APP_EXIT`。CDB 栈确认根因位于
`asio2::timer`：`PxPluginContext::OnDestroy()` 原先只调用 `stop_all_timers()`，该接口仅向
timer IO 线程投递异步取消；主线程随后立即释放最后一个 `shared_ptr`，handler 持有的最后
引用会在 IO 线程内触发 timer/iopool 析构，此时内部 `std::thread` 仍为 joinable，最终
进入 `std::terminate`。

修复后，插件上下文在非 IO 的正常宿主卸载路径调用同步 `timer->stop()`。它会完成取消、
停止 IO pool 并 join timer worker，之后才清空智能指针和工作线程，因此
`OnDestroy -> FreeLibrary` 之间形成真实卸载屏障。若 shutdown 恰好由 timer callback
发起，则把 timer 的最后一个 `shared_ptr` 移交给独立 RAII `PluginTimerReaper`；回调返回后
由非 IO 线程执行相同 stop/join，避免 self-join。Reaper 自身在 DLL 卸载时停止并 join，
不使用 detached thread。修复没有新增裸指针；测试中的 `GetInstance` 函数和实例转换仅
位于文档明确允许的既有插件 ABI 边界。

最终结果：

| 门禁 | 结果 |
| --- | --- |
| 真实 FT DLL load/queued work/stop/destroy/unload | 10/10 PASS；每轮 64 个排队请求，单轮约 1.8 秒 |
| 插件上下文异步销毁 | 4 tests ×10 轮 PASS；覆盖 timer/work callback 内 shutdown、64 个排队延时任务取消、重复构造/销毁 |
| 全部 Render 插件按需重链 | 24/24 PASS；未运行 Rust、Web 或 `build_official.bat` |
| Render 插件 build tree/dist | 24/24 SHA-256 一致 |
| Render 插件 dist/90 | 24/24 SHA-256 一致，missing=0、mismatch=0 |
| `px_render.exe` build tree/dist/90 | 一致：`D2300A6D487278B0EA2EF487C9AD2F91555F230AE7C8146FFE49538D16E3BC3C` |
| 最终部署后 90 原生 WS FT | 1 MiB 上传、下载、SHA-256、删除连续 10/10 PASS |
| 测试资源 | 临时 user/session/ticket 为 0；90 测试文件残留 0 |

共享 `px_plugin` 被静态链接到 Render 主程序和各 Render 插件，因此本次没有只发布
`ft.dll`：先由 `build_cpp_render_plugins.bat` 重链并发布全部 24 个插件，再由
`build_cpp_render.bat` 重链并发布 `px_render.exe`，最后在 90 的停服窗口原子替换并恢复
`px_service`。最终服务为 Running，20371 可达。

### 12.15 Phase 7 RTC 配置更新的第一批 awaitable 收敛

标准 RTC 原先已经能够调用 `SetConfiguration/RestartIce`，但控制流仍然分散：ICE failed
callback 只发一个消息，Panel 回传配置后同步返回 `true`，真正的 libwebrtc 应用结果、
后续 ICE connected/completed、30 秒宽限期和 Stop 分别由不同 callback 与 16 ms tick
维护。调用成功不等于重启成功；快速连续下发 revision 时也没有统一的重复、过期和迟到
结果规则。

本批增加 `RtcIceRestartWorkflow`，并让 Windows Client 的标准 RTC 路径使用同一个
`PxAsyncOneShot<RtcIceRestartCompletion>` 等待完整终态：

- 首次 ICE failed 建立 `awaiting configuration` 工作流，只发送一次配置请求；
- Panel 回传 revision 后进入 `applying configuration`，成功应用后等待 ICE
  connected/completed，任一步失败或 30 秒超时都返回结构化错误；实机 TURN 回归表明 15 秒会在候选检查完成前约 0.5 秒误杀仍持续传输媒体的会话；
- 同 revision 幂等忽略，已完成的旧 revision 判为 stale；更高 revision 更新当前工作流，
  旧 apply sequence 的迟到成功/失败不能结束新配置；
- ICE connected/completed、应用失败、超时和 Stop 竞争时只有一个终态；取消后迟到
  libwebrtc callback 被忽略；
- 移除 16 ms tick 中的重启倒计时，deadline 由 Asio timer 驱动，不再把 UI 帧定时器当作
  网络请求时钟；
- `MessageNotifier` 只读暴露其共享 `PxAsyncRuntime`，RTC 建立独立 state-lane
  `PxAsyncScope`，连接 Stop 会先取消操作并等待协程退出，不为每条连接再创建一套 Asio
  线程；
- libwebrtc adapter、Observer 和插件实例 ABI 均未改造，既有 WebRTC 裸指针边界保持原样；
  本批新增的 GammaRay C++ 没有裸指针和 `[this]` 捕获。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| RTC restart 状态机 | 6 tests × 10 轮，共 60 次，全部 PASS |
| 覆盖场景 | 请求→配置→恢复、重复/过期 revision、新 revision 替换、旧 apply 迟到失败、应用失败、超时、取消、10 次重复生命周期 |
| MessageNotifier 回归 | 10/10 PASS |
| PxAsyncRuntime 回归 | 10/10 PASS |
| Windows Client 增量编译 | `px_client` PASS；未调用 `build_official.bat`，未编译 Rust/Web |
| 本机 dist Windows Client → 90 标准 RTC | 10 次成功；RTC connected、首帧进入 UI、音频初始化、FT 通道就绪，临时 user/user-session/guest-session/ticket 每轮均清零 |
| 活跃标准 RTC 配置热更新 | 10/10 PASS；每轮第二张一次性 ticket + 最新 ICE snapshot，`SetConfiguration/RestartIce` 最终 completed，媒体、音频和 FT 会话保持工作 |
| revision 防护 | 10/10 PASS；每轮同时注入同 revision 与 revision-1，均记录为 duplicate/stale 并拒绝重复应用 |
| C++ ownership / `git diff --check` | PASS |

批量原生验收还暴露了测试基础设施的一个非产品失败：连续创建临时账号会命中 Console
每 IP 每小时游客 session 上限。`run_native_auth_case.ps1` 现按明确的 429 状态做有界退避
重试；本次为了继续验收只重启了 Console 的 `-r=server` 子进程清理内存限流状态，GUI 和
Mongo 数据未重启。自签名 HTTPS 由 curl `--insecure` 访问，请求 JSON 通过 stdin 发送；
只读 TLS warm-up 不再额外消耗 guest session。

客户端运行产物已由按需发布脚本同步到 `build_official\dist` 并逐项核对 SHA-256：

| 产物 | SHA-256 |
| --- | --- |
| `px_client.exe` | `53EF7DB91644D8385FDE20AD96652504A51E33BF93C58BD42773F39258781B10` |
| `px_client_rtc.dll` | `8CE3861C1A5308CDA8E8BB5035E4ADBB42DC066D9F10BBE6863FCC22798B8520` |
| `deps\ct_plugins\clipboard.dll` | `37FEDCB8493F84A1234178ACB6E61811FAA6B1FD9D2AB65B1F94A95EE44BC11F` |
| `deps\ct_plugins\ft.dll` | `EF98A089D420AE05B946D3E6BA1379E25D3FBD4FB9C5BB29A3F1072BDAE3576A` |
| `deps\ct_plugins\record.dll` | `C96EA5F16B2383D0E4B4F83EEA4DC28AFDF725A97D5F15E27589112D4FC30705` |

当前 Console 管理员密码已不是仓库测试默认值，因此实机验收没有修改生产 RTC 配置。
验收使用仅监听 `127.0.0.1` 的受控 Panel WebSocket：等待真实 dist 客户端 Hello 后，
下发由 Console 普通用户 API 实际签发的第二张 ticket、对应 ICE snapshot 和新 revision。
这覆盖了产品中的 Panel→Windows Client→SDK→libwebrtc 热更新链路，同时避免管理接口
权限依赖。10 轮均出现 Panel command、restart started、ICE completed，随后 duplicate/stale
保护生效；视频帧、音频和 FT 通道验收同时通过。真实 Console 管理端“修改配置并广播”
仍属于管理控制面的独立验收项，不再阻塞客户端活跃会话热更新结论。

### 12.16 Phase 7 Panel RTC 配置拉取的 awaitable 收敛

Panel 收到 Console 的 RTC 配置 revision 通知后，原实现通过 detached thread 同步访问
HTTPS API。该线程不属于任何可停止作用域，Panel Stop 无法等待它；拉取期间的新 revision
会被一个 atomic refreshing 标志直接丢弃，可能使 Panel 永久停留在旧配置。

本批将该上游拉取链迁移到共享 `PxAsyncRuntime`：

- `PanelRtcConfigRefreshGate` 保证同一时刻只有一个 HTTP 拉取；拉取期间到达的请求合并为
  恰好一次后续拉取，并携带所见最大 expected revision；
- 既有同步 `HttpClient` 作为 5 秒超时的阻塞叶子投递到网络线程，结果通过
  `PxAsyncOneShot` 回到协程；整个 await 另有 6 秒 deadline；
- Panel Stop 先关闭 gate，再取消并最多等待异步 scope 2 秒，取消后的迟到 HTTP 结果不能
  访问已销毁的 Panel，也不能再发布 `MsgRtcIceConfigUpdated`；
- 协程不会跨 `co_await` 持有 Panel 的 shared ownership，恢复时重新 `weak_ptr::lock()`；
  同时删除了 `PxConsoleClientImpl` 内长期保存的 `PxSettings*`；
- 没有修改 libwebrtc 借用指针边界或插件 instance ABI，也没有引入 detached thread、
  裸指针和 `[this]` 捕获。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| Panel refresh gate | 7 tests × 10 轮，共 70 次 PASS |
| gate 覆盖 | 首次启动、并发合并、最新 revision、启动回滚、Stop/迟到完成、32 线程并发、10 次重复生命周期 |
| 共享 PxAsyncRuntime | 11 tests × 10 轮，共 110 次 PASS；包含排队取消、callback 内 Stop 和重复 start/stop |
| Panel 按需编译 | `px_panel` PASS；只编译 Panel 相关 C++，未运行 `build_official.bat` |
| dist 启动 smoke | `px_panel.exe` 从 `build_official\dist` 启动并保持 15 秒；Service WebSocket、Render WebSocket 与 HTTPS Console 均连接成功 |
| C++ ownership / `git diff --check` | PASS |

Panel build tree 与 `build_official\dist` 的 `px_panel.exe` SHA-256 一致：
`B72D77FB2CCE9DA8C859D91069C0D08DC1DD4B5FBBEDA7C7F35C2C2359E4981D`。
本批没有 Console 管理员凭据，因此没有改动生产 RTC 配置来触发真实管理端广播；该项仍保留
为管理控制面验收，不影响本批对 Panel 拉取并发、取消和生命周期安全的结论。

### 12.17 Phase 7 Panel 录像回传上传的 awaitable 收敛

Console 请求 Panel 回传录像文件时，原实现创建一条专用 `std::thread`，在其中串行执行
最长 1 小时的同步 multipart 上传和 `sleep_for` 重试。正常 Stop 会直接 join，网络异常时
可能阻塞退出；若 Stop 恰好从该 worker 发起则改为 detach，线程随后仍可能访问 Panel。

本批将业务编排改为有界 awaitable workflow：

- `RecordFetchQueue` 现在同时维护队列、去重集合和唯一 pump 所有权；生产者与 pump 变空
  竞争时由同一 mutex 决定继续或重新启动，不会丢唤醒，也不会出现两个并发上传；
- Panel 使用共享 `PxAsyncRuntime` 的独立 `PxAsyncScope` 运行串行 pump；重试退避由可取消
  Asio timer 驱动，不再用 100 ms 轮询睡眠；
- 同步 CPR 上传保留为阻塞叶子，但放入 Panel 自有、可 join 的单线程 `TaskRuntime`，不会
  占用 `PxContext` 的共享网络线程，也不会阻塞 RTC 配置拉取；
- `HttpClient` multipart 增加 `shared_ptr<atomic_bool>` 取消信号，并直接接入 CPR/libcurl 的
  cancellation progress callback。Stop 先停止队列并置位当前上传信号，再取消 scope、等待
  协程退出并 join 阻塞 worker；1 小时是正常大文件请求上限，不再是退出等待上限；
- 阻塞叶子只捕获 task、appkey、one-shot 和取消信号，不捕获 Panel；协程跨 `co_await`
  不持有 Panel shared ownership，迟到结果只能尝试完成 one-shot，不能访问已销毁对象；
- 删除 `fetch_thread_`、`FetchWorkerLoop`、`WaitPop` 和 self-thread detach 路径，没有新增
  裸指针、`[this]` 捕获或跨 DLL awaitable ABI。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| record fetch 队列/解析 | 24 tests × 10 轮，共 240 次 PASS |
| 队列覆盖 | FIFO、去重、重试入队、32/64 线程并发、pump 启停竞争、Stop、Abort、迟到 retry、10 次重复生命周期 |
| 真实 multipart 取消 | 3 tests × 10 轮，共 30 次 PASS；覆盖传输中取消、开始前取消、受管 worker 取消后 join |
| 共享 PxAsyncRuntime | 11 tests × 10 轮，共 110 次 PASS |
| Panel 按需编译 | `px_panel` PASS；未运行 `build_official.bat`，未编译 Rust/Web |
| dist 启动 smoke | 新 `px_panel.exe` 保持运行 15 秒；Service、Render、HTTPS Console 连接成功，单线程上传 worker 启动 |
| C++ ownership / `git diff --check` | PASS；record fetch 旧线程、`WaitPop`、detach 搜索为零 |

Panel build tree 与 `build_official\dist` 的 `px_panel.exe` SHA-256 一致：
`E1D32E1F4DCCD151A489227DB0BAD475CCF9405439EC6468DC990D5B563F38F4`。
真实 HTTP 测试使用本机慢速 TCP 接收端和 32 MiB multipart 文件，验证的是 CPR 的实际
传输中断与 worker join，不是 mock。当前没有使用 Console 管理端触发生产录像回传命令，
因此“Console 页面请求真实录像并完整下载”仍作为功能级独立验收项保留。

### 12.18 Phase 7 Panel 录像列表请求的 awaitable 收敛

`RecordListReq` 原先直接在 asio2 WebSocket `bind_recv` 回调中调用
`ScanRecordFiles`。录像目录中文件较多或磁盘响应慢时，同一 Console 连接上的 Hello、心跳、
RTC 配置通知和录像回传命令都会被同步扫盘阻塞。

本批把录像列表变成有界请求流程：

- `RecordListRequestGate` 最多允许 4 个在途请求；超过上限立即返回明确错误，Stop 原子拒绝
  新请求并置位所有扫描取消信号；
- `ScanRecordFiles` 接受共享原子取消信号，在开始和每个目录项之间检查，取消后不返回半份
  文件列表；
- 每个请求由独立 `PxAsyncScope` coroutine 编排，实际文件系统扫描投递到 Panel 自有的
  单线程 `TaskRuntime`，不会占用 Console WebSocket、RTC 配置或录像上传 worker；
- 单次 await deadline 为 30 秒；完成、异常、超时、Stop 和迟到结果只产生一个终态，
  request sequence 的重复/迟到 Finish 为幂等；
- coroutine 跨 `co_await` 不持有 Panel，阻塞叶子只持有目录、one-shot 和取消信号；
  protobuf 返回对象只在第三方 ABI 边界瞬时解引用，没有新增裸指针或 `[this]` 捕获。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| 录像目录与请求 gate | 33 tests × 10 轮，共 330 次 PASS |
| gate 覆盖 | 4 请求上限、容量恢复、32 线程竞争、Stop 全取消、迟到/重复 Finish、零容量钳制、10 次重复生命周期 |
| 可取消扫描 | 预取消不读取/返回文件；既有 sidecar、文件名、时间、Range 行为全部回归 |
| 录像上传回归 | 24 tests × 10 轮，共 240 次 PASS |
| 真实 HTTP 取消 | 3 tests × 10 轮，共 30 次 PASS |
| 共享 PxAsyncRuntime | 11 tests × 10 轮，共 110 次 PASS |
| Panel 按需编译 | `px_panel` PASS；未运行 `build_official.bat`，未编译 Rust/Web |
| dist 启动 smoke | 新 Panel 保持运行 15 秒；Service、Render、HTTPS Console 均连接成功，两个隔离 worker 均启动 |
| C++ ownership / `git diff --check` | PASS |

Panel build tree 与 `build_official\dist` 的 `px_panel.exe` SHA-256 一致：
`4A0326D3907E49DDF5F743418FFBDDFE9FED9CE08D8C8A2DE8059DC8F5849381`。
本批验证了真实目录扫描、并发/取消状态和生产 Panel 启动链；没有使用 Console 管理页面下发
真实 `RecordListReq`，因此管理页面端到端列表展示仍保留为独立功能验收项。

### 12.19 公共任务异常边界与 Panel SQLite 锁争用修复

发布 12.18 后的延长 smoke 发现 Panel 在资源刷新线程中退出。Windows WER 与本机 dump
确认异常链为 `AppStreamList::RefreshRemoteDevices -> StreamDBOperator::UpdateStream ->
sqlite_orm::throw_translated_sqlite_error -> std::terminate`，SQLite 原因是 `database is
locked`。历史 CrashDumps 中在本批改动前已有多次相同进程退出，因此该问题不是录像列表
awaitable 引入，但会干扰所有 Panel 长时间验收。

本批增加两层处理：

- `Thread` 的队列任务与一次性任务入口捕获标准和非标准异常，记录线程名、任务 ID 和错误，
  并保证任务状态、执行计数及后续队列继续推进；任意业务任务异常不再越过线程入口触发
  `std::terminate`；
- Panel 创建 sqlite_orm storage 时统一配置 3 秒 busy timeout。sqlite_orm 会把 `on_open`
  策略复制到每个 storage 副本，因此所有惰性创建的 SQLite 连接都会有一致、有界的锁等待，
  短暂写锁不再立即失败；
- 数据库损坏备份辅助函数同时移除了旧的输出裸指针参数，改为 `optional<string>` 结果，
  新增代码没有裸指针或 `[this]` 捕获。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| Thread 异常边界 | 7 tests × 10 轮，共 70 次 PASS；覆盖标准异常、非标准异常、后续任务继续、once task、self-stop 和 10 次生命周期 |
| SQLite 锁等待 | 2 tests × 10 轮，共 20 次 PASS；真实双连接写锁等待 100 ms，释放后第二写入成功；storage copy 的 busy timeout 为 3000 ms |
| 公共异步回归 | TaskRuntime、MessageNotifier、PxAsyncRuntime 各 10/10 轮 PASS |
| Panel 按需编译 | `px_panel` PASS；未运行 `build_official.bat`，未编译 Rust/Web |
| dist 延长 smoke | 真实复现旧锁异常后，防崩版本同 PID 110 秒；根治版本同 PID 120 秒，Console hello 与设备链接更新正常 |
| 日志与系统事件 | 根治版本启动后无 `database is locked`、未捕获任务异常、terminate/fatal 或新的 WER/Application Error |
| C++ ownership / `git diff --check` | PASS |

最终 `px_panel.exe` 已同步到 `build_official\dist`，build tree 与 dist 的 SHA-256 均为
`D087006FF8D2830B9C0CE155DE2A664F0502A7CFB4E695AB72D761654C6992FD`。

### 12.20 Console 录像管理真实页面与隧道验收

12.18 只关闭了 Panel 侧列表并发和取消状态机，没有覆盖 Console 管理页面。后续将
`scripts/cdp_records_e2e.mjs` 改为生产级页面验收，不再注入伪 token 或测试 appkey：

- 使用当前管理员账号从真实登录页登录，依靠 Cookie 和 CSRF 完成后续操作；密码只从环境
  变量读取，不写入仓库、日志或截图；
- 调用真实 `/record/list`，并在 CDP 中仅阻断 Panel 的 20369 直连，强制走
  `Console -> RecordFetchReq -> Panel multipart upload -> Console cache` 生产隧道；
- 删除既有 Console 缓存后重新拉取，验证 HTTPS 视频请求返回 200/206、可以播放并 seek；
- 在页面上执行保留、删除并复查最终列表，同时收集浏览器异常和网络失败；
- 每轮结束清理 Chrome profile、Console 上传缓存和测试进程，不输出 query ticket。

真实 Console 页面连续 10/10 PASS。90 Panel 日志对应出现 50 次 `RecordListReq`、10 次
`RecordFetchReq`、10 次上传成功和 10 次协程回到 idle，错误为 0；Console 本轮错误为 0，
上传暂存目录为空。该结果关闭了 12.18 中保留的“管理页面端到端列表、回传、播放和删除”
验收项。

### 12.21 Windows 原生 FT UI、并行会话与取消验收

新增 `scripts/run_native_ft_ui_e2e.ps1`，所有功能验收强制从
`build_official\dist\px_client.exe` 启动真实 `--mode=file-transfer` Qt 窗口。脚本通过
Console 普通用户 API 注册、登录并签发真实 ticket，再使用 Windows UI Automation 和物理
鼠标操作两侧文件栏、传输箭头、冲突弹窗和传输队列；不能绕过 UI 直接调用 FT 引擎。

| 场景 | 结果 |
| --- | --- |
| 1 MiB 双向上传、下载、SHA-256、删除 | 10/10 PASS |
| 64 MiB 双向上传、下载、SHA-256、删除 | 10/10 PASS；累计约 1.28 GiB |
| 10000 小文件 | PASS；100 个 Unicode/空格目录、空目录，上传下载聚合摘要 `8FD720ECE1B0B808D14A3D68519B9761B3ED45977AB30678216F5693A1CA4C2E` |
| 真实覆盖弹窗 | 10/10 PASS；目标最终为新内容 SHA-256 |
| 真实跳过弹窗 | 10/10 PASS；目标保持预置内容 SHA-256，并可从 UI 下载回读 |
| 真实取消按钮 | 10/10 PASS；每轮启动 1 GiB 上传后从展开的队列点击取消，客户端存活且没有完整目标落盘 |
| 标准 RTC 与独立 FT 窗口并存 | PASS；远控视频解码、音频初始化和 FT 通道保持工作，同时 FT 窗口 1 MiB 双向 10/10 |
| 真实 DLL 卸载复核 | 10/10 PASS；每轮 `LoadLibrary`、64 个排队请求、`OnStop/OnDestroy/FreeLibrary` |

脚本支持 `Normal / Overwrite / Skip / Cancel` 四种模式，失败和成功路径都会停止测试客户端，
等待文件句柄释放，并重试清理正式文件、`.download` 和 `.digest`。最终本机与 90 的
`px_ft_ui_*` 文件残留均为 0，临时 user/session/ticket 均为 0。90 的 C 盘可用空间不足 2 GiB，
因此没有重复 12.13 已完成的 1 GiB 双向完整传输；本节 1 GiB 只用于即时取消，避免填满测试机。

本批只按需构建 `test_ft_plugin_dll_lifecycle`，没有执行 `build_official.bat`，也没有编译
Rust 或 Web。按需重链的 Render `ft.dll` 已在停止对应 Render 进程后同步到 dist，服务自动
拉起新 Render；build tree 与 `build_official\dist\deps\rd_plugins\ft.dll` 的 SHA-256 均为
`CF89AE9D203190811A0E5CB97A160DED7C969CA1818A04942B43FE9BF51317C7`。

### 12.22 Phase 7 连接、认证前置与自动重连的 awaitable 收敛

本批新增公共 `PxConnectionAttemptWorkflow`，把“TCP 已连接”“WebSocket upgrade 已完成”以及
“上层可以发送 Hello、认证或业务消息”从分散回调收敛为一条带 generation 的有界 awaitable
状态流。每次 asio2 `bind_init` 创建一代 attempt；只有 upgrade 成功才能 `MarkReady`，连接失败、
upgrade 失败、断开、超时、替换和 Stop 都产生类型化终态。旧 generation 被新连接替换时立即取消，
迟到事件不能再次完成；ready 后断开会清除可发送状态，但不会重复调用已完成回调。

接入范围：

- Windows Client → Panel：ready 后才发送 Panel Hello；
- Windows Client → Console：每代连接重新生成短期 token，ready 后才发送 Console Hello；
- Panel → Console：ready 后才发送 Hello 并启动 RTC 配置刷新；
- Panel → Service：ready 后才发布已连接事件和认证信息，心跳及普通消息不能在 upgrade 前发送；
- Render → Service：ready 后才发送待处理 instance-ready 并发布 `MsgRenderConnected2Service`；
  断线同时使所有待处理 ticket/虚拟显示器 RPC 以可重试错误结束；
- Render → Panel：ready 后才发布首轮统计和插件信息，普通消息发送同时受 ready 状态门禁。

所有网络、timer、scope completion 和 UI/task queue 回调继续使用 `weak_ptr::lock()`；新 workflow
只保存 `shared_ptr`，没有裸指针或 `[this]` 捕获。libwebrtc 借用 ABI 与插件 instance ABI 未改动。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| connection workflow | 9 tests × 10 轮，共 90 次 PASS |
| 状态覆盖 | ready 单终态、类型化失败、新代替换、可重试超时、ready 后断线清状态、ready 前 Stop、析构时排队完成、completion 内 Stop、10 次重复生命周期 |
| Panel / Render 按需编译 | `px_panel`、`px_render`、`test_connection_attempt_workflow` PASS；未运行整体 `build_official.bat`，未编译 Rust/Web |
| Render → Panel 真实重连 | 显式从 dist 重启 Panel 10 轮，10/10 ready；Render PID 全程不变，generation 15→24 |
| Service 托管重启 | 重启 Windows `px_service` 10 轮；每轮 Service 和托管 Render 均换新 PID，20371 恢复监听，Render→Service Established 且新增 await-ready 日志，10/10 PASS |
| 标准 RTC 功能回归 | dist Windows Client、账号 ticket、90 机器、标准 RTC 10/10 PASS；每轮视频 UI、音频初始化、FT 通道均正常，临时 user/session/ticket/guest-session 均为 0 |
| C++ ownership / `git diff --check` | PASS；无新增裸指针、手工所有权或 `[this]` 捕获 |

发布后的 build tree 与 `build_official\dist` 哈希一致：

- `px_panel.exe`：`7BFA6CD2BADC4A50E79D132D13187E96C46A1B7C06108140AE6F111D1C307BC6`；
- `px_render.exe`：`7D5B2B2D6E907F461319A01C1E93476EC6400E2C2FFAFE51274C07F4817C3920`。

该批关闭的是连接和自动重连的回调状态错位；登录后的 Console `StartApp → QueryApps →
IssueTicket → endpoint probe` 仍包含 UI 线程轮询与同步等待，作为下一批认证编排迁移范围。

### 12.23 Phase 7 Panel 启动授权编排的 awaitable 收敛

本批新增 `StreamLaunchAuthWorkflow`，把登录态设备和云应用启动前的同步链路从 Qt UI
线程迁移到共享 `PxAsyncRuntime` 的 worker lane。设备路径按顺序执行 `IssueDeviceTicket →
resolve endpoint → optional direct probe`；应用路径执行 `StartApp → cancellable timer poll
QueryApps → IssueInstanceTicket → resolve endpoint → optional direct probe`。独立文件传输入口复用
同一 workflow，但只申请 `file` 权限并继续使用可靠 WebSocket 直连或 Relay。

- 每次启动分配 generation；新请求取消旧请求，UI 只接受当前 generation，迟到结果不能启动
  客户端或覆盖卡片状态；
- 应用轮询使用 Asio timer，不再在 UI 线程 `sleep_for`；同步 Console HTTP 和 endpoint probe
  作为有界阻塞叶子投递到 Panel 既有任务运行时；
- Stop 先置位取消标志，再停止并等待 scope。阻塞叶子不捕获 widget/workflow，有限超时后的
  迟到完成只会尝试结束 one-shot，不能访问已销毁 UI；
- Console API 错误保留 stage、稳定错误码和服务端信息；空 instance ID、非法 endpoint、超时、
  替换和 Stop 都有类型化终态；
- 游客/显式 IP 密码直连不进入 ticket workflow。登录态 Console 设备和云应用仍只使用 ticket；
- 新增和本次触及的异步回调使用 `QPointer`、`shared_ptr` 或 `weak_ptr`，没有新增裸指针、
  `[this]` 捕获，也没有改动 libwebrtc 或插件 instance ABI。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| 启动授权 workflow | 11 tests × 10 轮，共 110 次 PASS |
| 状态覆盖 | 设备 ticket、应用 starting→running、空 instance ID、配置关闭探测、强制 Direct、强制 Relay、Console 类型化失败、deadline、新代替换、Stop/迟到完成、10 次重复生命周期 |
| 连接策略与 Console 错误兼容 | `test_connection_policy`、`test_console_api_error` 各 10 轮 PASS |
| Panel 按需编译 | `test_stream_launch_auth_workflow`、`px_panel` PASS；仅使用 C++ focused target，未运行整体 `build_official.bat`，未编译 Rust/Web |
| 账号设备真实连接 | 本机登录态 Panel 点击 90 的 `001190520` 卡片连续 10/10 PASS；generation 1→10，零授权失败，每轮客户端响应且建立两条到 `10.0.0.90:20371` 的连接；首轮实拍确认 90 桌面画面，不是白屏 |
| 显式 IP 直连回归 | `10.0.0.90` 卡片以 `conn_type=direct` 启动，未增加 Console workflow generation；视频首帧进入 UI、FT 通道连接，证明密码直连分支未被强制改成 ticket |
| 独立文件传输入口 | generation 11 完成 file ticket 与探测，`dist` 客户端打开；本机和 90 两侧真实目录均成功列出 |
| 云应用授权链 | `baidu` 的 StartApp、实例状态、QueryApps、实例 ticket、endpoint probe 和客户端启动通过；Panel 轮询期间 0 次无响应，实例 ID 正确传给客户端 |
| C++ ownership / `git diff --check` | PASS |

云应用的完整数据面验收没有通过：90 上实例进入 `running`，端口 `32010` 的配置探测成功，
但客户端随后停在“等待配置信息”，没有远端画面；90 同期创建的 `rec_webview` 文件为 0 字节。
这发生在本批 awaitable 授权链完成之后，不能否定 UI 解阻和 ticket 编排结果，但也不能把云应用
画面宣称为通过。测试实例 `inst-6-e4db6968` 已通过 Console Stop API 恢复为 `stopped`；后续应
独立排查 90 的 WebView capture / inner Render `32010` 数据面以及 Direct RTC 实际建连失败后的
标准 RTC 回退。

新 `px_panel.exe` 已同步到 `build_official\dist`，build tree 与 dist 的 SHA-256 均为
`6A6C9FA0023947FDB807603F2C4F5A74965942BC7F3C41A29F18B093E3D531A5`。

### 12.24 WebView 云应用 RTC 数据面与标准 RTC 回退闭环

12.23 记录的云应用无画面由四个相互独立的问题叠加造成，本批逐层关闭：

- inner WebView capture 启动时 Windows 显示器枚举可能为空。Render 现在允许首个真实捕获源
  认领一个预协商 RTC track slot，并在后续拓扑枚举到达时保持稳定映射；拓扑重绑会重置帧序，
  不再把新流的低帧号误判为旧帧；
- WebView 的 NVENC H.264 新订阅者需要可独立解码的首帧。软件编码和 NVENC 都执行统一的
  join-safe IDR 策略，避免 Direct RTC 已连接但长期等待关键帧；
- Console 实例 ticket 已经返回精确的 `signal_device_id`，但原生 Panel/Client 原先丢弃该字段。
  现在 endpoint 的基础设备 ID 仍用于 ticket、探测和显示，RTC/Relay 信令单独使用精确的
  `server_<device>__instance__<instance>`；Direct 失败后的标准 RTC 不再错误进入桌面 Render；
- 标准 RTC 由 libwebrtc 输出 CPU I420，原 D3D11 控件却无条件要求帧自带 GPU device/texture，
  因而出现“首帧进入 UI”后持续白屏。CPU 帧现在复用 Client 初始化阶段持有的
  `D3D11DeviceWrapper`，把 Y/U/V 平面上传到动态纹理；硬解 D3D11 texture 路径保持不变。

本批没有执行 `build_official.bat`，仅用 C++ focused target 构建
`test_client_virtual_display`、`test_stream_launch_auth_workflow`、`test_connection_policy`、
`test_console_api_error`、`px_panel` 和 `px_client`，并沿用前一阶段已经完成的 Render/插件
focused build。没有重新编译 Rust 或 Web。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| RTC identity / topology / keyframe / virtual-display 单测 | 32 tests × 10 轮，共 320 次 PASS |
| 启动授权、连接策略、Console 错误回归 | 3 个测试程序各 10/10 PASS |
| C++ ownership / `git diff --check` | PASS；无新增裸指针、手工 ownership 或异步 `[this]` |
| ticket 自动回退路由 | 主动终止 Direct 客户端后，Panel 自动启动标准 RTC；room remote 精确为 `server_001190520__instance__inst-11-a204a9c4`，配置为 `webview 1920×1080` |
| 修复后真实标准 RTC 画面 | 本机 dist Client 以游客密码连接 90 的同一 WebView 子实例；实际百度页面可见，D3D11 I420 初始化成功，旧 `No device with texture`/新缺少 device 错误均为 0 |
| 真实重复连接 | 同一 WebView 子实例标准 RTC 建连、1920×1080 首帧、D3D11 上传、客户端响应和关闭连续 10/10 PASS |
| 90 状态 | WebView instance `inst-11-a204a9c4` 运行，Render 数据端口 32010 可用；`px_service` 正常 |

发布和部署哈希如下，所有 build tree 与 `build_official\dist` 对应项一致；三个 Render 产物
同时与 90 安装目录一致：

- `px_client.exe`：`5AF8A1D5041A83BE097CE77A721AE03353D02783E0C72A3C28BB6278C6BA8C45`；
- `px_panel.exe`：`8FEA11803FC2A424097A50E47FE23752FB812569973A5CFFB1D326D25C0D5BDD`；
- `px_render.exe`：`ED83F7BF9ECDC5C61420AEBB566971BAD8A447265FB5A9F7B87FC5A6A59BCDD2`；
- `net_rtc_local.dll`：`ACF0ABA449F176322B83D939696D06C7270697F21215C49492B3A04001753001`；
- `enc_ffmpeg.dll`：`23461781E7CB0FCA307967126A895176D3C53D33BED643E4AA3BC8CDA7E2EA3A`。

因此 12.23 的“云应用数据面未通过”已经关闭。Panel 在发布客户端时被重启，内存中的登录
会话随之退出，所以修复后 10 轮使用了产品支持的游客密码分支；ticket 自动回退的实例路由和
游客标准 RTC 的最终画面分别完成了实机验证，两者进入相同的 Client 标准 RTC 解码/渲染路径。

### 12.25 登录态 Direct RTC 实际失败后的标准 RTC 自动重开

12.24 已分别验证 ticket 路由和游客标准 RTC 画面，但登录态端到端测试暴露出一个状态判定
问题：Client 启动后会先连本机 Panel WebSocket 并发送 `kCpHello`，而
`RunningStreamManager` 原先把这个本地进程握手当作远端 RTC 已经建连。于是 Direct probe
虽然成功，只要 Direct RTC 在实际建连阶段失败或进程退出，Panel 也会因为“已连接”而抑制
标准 RTC 回退。

本批把两个状态明确分开：

- `kCpHello` 只表示本机 Client 与 Panel 的控制通道可用；
- Client 收到 SDK 的 `SdkMsgNetworkConnected` 后，才发送新的
  `kCpTransportConnected`；该消息是 Panel 关闭加载框和禁止 Direct 回退的唯一远端传输
  就绪依据；
- 网络断开、Panel WebSocket 重连和 Client 退出均会重置上报门闩；如果远端传输先于 Panel
  WebSocket 就绪，`Hello()` 会补发已锁存的传输就绪状态；
- Panel 连接状态由 `DirectRtcFallbackState` 封装并受既有 mutex 保护，避免本地控制通道和
  远端数据通道再次混用。

本批仍未执行 `build_official.bat`。仅使用 focused C++ target 构建
`test_direct_rtc_fallback_state`、`px_client` 和 `px_panel`，随后按发布规则同步到
`build_official\dist`。

专项验收如下：

| 门禁 | 结果 |
| --- | --- |
| 状态门单测 | 5 tests × 10 轮，共 50 次 PASS；覆盖初始状态、仅 Panel 通道、真实传输就绪、protobuf 往返和 10 次状态重建 |
| 相关 C++ 回归 | `test_connection_policy`、`test_stream_launch_auth_workflow`、`test_panel_rtc_config_refresh_gate`、`test_client_virtual_display` 各 10/10 PASS；连同状态门共 650 个测试通过 |
| 登录态 ticket 路由 | Panel 使用既有登录账号，从 Console 获取 90 上 `baidu` 实例 ticket；标准 RTC 的信令目标连续保持为 `server_001190520__instance__inst-11-a204a9c4` |
| Direct 实际失败注入 | 每轮等待 `webrtc_direct` Client 启动后终止该进程，模拟 probe 成功但实际建连失败；Panel 自动刷新 ticket 并重开 `network_type=webrtc` |
| 真实传输就绪 | 标准 RTC Client 每轮均向 Panel 上报 `Client remote transport connected`，加载状态不再被本地 Panel WebSocket 提前结束 |
| 真实画面 | 标准 RTC 回退后的 dist Client 显示 90 上实际百度 WebView，非白屏；无 `No device with texture`、`Cannot render frame format`、`Invalid CPU`、`Failed to map CPU` |
| 重复验收 | 成功轮次 10/10 PASS；另观察到一次未主动终止时的 Direct connect timeout，同样自动重开标准 RTC 并成功上报传输就绪 |
| 环境说明 | 本机 Windows Firewall 的 Domain/Private/Public profile 均为关闭，因此未把防火墙规则作为有效故障注入；本轮没有遗留临时阻断规则 |
| 测试数据清理 | 未使用的临时 Console 用户、ticket、session、group member、device 和 event 均已删除，复查计数为 0 |

最终发布哈希（build tree 与 `build_official\dist` 对应项一致）：

- `px_client.exe`：`2381937BB1D7E1B1C00FE817E0738E118980186D61187AB291304B869E65C36B`；
- `px_panel.exe`：`01674FE78C0D4ED6D082DC3FADC641E4D524187F303752748F6E8AF5A168A7F4`。

### 12.26 Phase 7 Console UDP 发现接收器的 awaitable 收敛

Panel 的 Console 自动发现原先使用一条独立 `Thread`：UDP socket 被设为 non-blocking 后，
每 100 ms 轮询一次；端口或接收异常时再阻塞 sleep 2 秒。退出依赖轮询观察 atomic 标志，
接收、重试、消息解析和对象销毁没有同一个可取消生命周期。

本批新增 `ConsoleDatagramReceiver`，将这条链迁移到 MessageNotifier 已有的共享
`PxAsyncRuntime` state lane：

- coroutine 使用 `async_receive_from` 挂起等待，不再轮询或占用独立线程；
- bind/receive 失败后用可取消 `steady_timer` 有界重试，不再 `sleep_for`；
- socket、timer、buffer 和 sender endpoint 都由智能指针持有；Stop 会先置位停止状态，再在
  同一 executor 上 cancel/close socket 和 timer，最后停止并等待 `PxAsyncScope`；
- handler 只负责把一个完整 datagram 交回 `ConsoleScanner`。解析、设备表更新和消息发布保留
  原语义；handler 异常被隔离，不会结束接收器；
- 回调内 Stop 不同步等待所在 scope，外部随后 Stop/析构会完成收敛；重复 Start 被拒绝；
- 没有修改 Console 广播格式、`console://access##`/`cms://access##` 兼容解析、插件 instance、
  WebRTC 或任何第三方代码。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| 真实 UDP receiver | 9 tests × 10 轮，共 90 次 PASS |
| receiver 覆盖 | loopback datagram、重复 Start、挂起 receive 时 Stop/析构、回调内 Stop、端口占用后恢复、重试 timer 中 Stop、handler 异常后继续、10 次内部生命周期 |
| 公共异步回归 | `test_async_runtime` 11 tests × 10、`test_message_notifier` 32 tests × 10，共 430 次 PASS |
| Panel focused build | `px_panel` PASS；未运行 `build_official.bat`，未编译 Rust/Web |
| 真实 dist 生命周期 | `build_official\dist\px_panel.exe` 启动、PID 持有 UDP 30501、退出释放端口连续 10/10 PASS |
| 生产 smoke | 最终 dist Panel 绑定 UDP 30501，Console WebSocket ready，Service Running |
| 旧实现残留 | `udp_receiver_thread_`、non-blocking receive、100 ms/2 秒 polling sleep 均为 0 |

Panel build tree 与 `build_official\dist` 的 `px_panel.exe` SHA-256 一致：
`8D365A6BF2C3A66A4C1ECE3B043530FF3465D6AC0FE6CDEEC17D2D82CBF9F9BC`。

### 12.27 Phase 7 Relay 插件后台生命周期与无裸指针事件源收敛

Render 的 Relay 插件原先在 `OnCreate` 中启动永久 detached 线程，并在监控线程、Relay SDK
收包回调及媒体发送任务中直接捕获插件 `this`。`OnDestroy` 只能停止当时可见的 SDK，不能停止
监控线程；线程随后仍可重建 SDK、访问已卸载 DLL 中的插件实例。2 秒连接轮询和 500 ms 重试
也不可取消。该问题会直接放大为 Render 退出卡死、重复连接后崩溃或文件传输状态串线。

本批在不修改 `GetInstance`、插件实例身份、加载/卸载 ABI 和 libwebrtc 结构的前提下完成以下
收敛：

- 新增 `RelayPluginRuntime` 共享运行态；Relay 插件本身只保留
  `atomic<shared_ptr<RelayPluginRuntime>>` 适配入口；
- 永久 detached loop 改为 owned `jthread`，所有等待可由 stop token 和 condition variable
  唤醒；Stop 将线程移出锁区后 request-stop、join，再同步停止媒体/文件 SDK，避免 join 持锁
  死锁；
- SDK 收包、心跳、房间、暂停/恢复、通知及发送队列回调全部捕获 `weak_ptr`；SDK 自身改为
  `enable_shared_from_this`，不再在 WebSocket 异步回调中捕获裸 `this`；
- 媒体与文件通道各有 generation。配置更新或重连会先废弃旧 generation，旧 SDK 的迟到
  回调不能再发布事件、修改路由或产生 ACK；host、port、device ID、appkey 更新均会唤醒并
  重建连接；
- `PxPluginNetClientEvent` 增加值语义 `source_plugin_id_` 和 owned ACK callback。新 Relay
  路径不再为了路由和 ACK 保留插件指针；原 `from_plugin_` 仅保留给既有插件 ABI；
- 文件传输 connection instance generation、严格消息序列检查、独立 FT 发送通道和背压信号
  保持原语义；媒体 pause 不会阻断独立文件传输。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| Relay DLL 真实生命周期 | 单次测试内 10 轮真实 LoadLibrary / Start / 配置更新 / Stop / OnDestroy / FreeLibrary，10/10 PASS；每轮模块句柄归零 |
| 故障覆盖 | 连接建立中停止、host/port/appkey 活跃更新、旧 generation 失效、重复创建销毁、排队回调销毁及 join 锁序 |
| 公共异步/插件回归 | `test_async_runtime` 11 tests、`test_plugin_context_lifecycle` 4 tests、`test_ft_plugin_dll_lifecycle` 1 test，全部 PASS；其中公共运行时与 Context 自带 10 轮重复生命周期 |
| focused C++ build | `net_relay`、`net_ws`、`net_udp`、`net_rtc`、`net_rtc_local`、`px_render` PASS；未运行 `build_official.bat`，未编译 Rust/Web |
| ownership 门禁 | `check_cpp_ownership.ps1` PASS；新 Relay 运行态、SDK 回调和测试没有新增裸指针、手工 ownership 或 `[this]` 捕获；测试中的插件指针仅为已标注的加载 ABI 边界 |
| 真实 dist 服务回归 | 本机 `px_service` 启停 10 轮，10/10 从 `build_official\dist` 拉起新 Render；每轮均恢复两条到 `10.0.0.16:30502` 的 Established Relay 连接（媒体 + 文件） |
| 最终状态 | `px_service` Running；dist Render 正常运行，Relay 媒体/文件双通道 Established |

由于事件结构增加了值语义来源和 ACK sink，本批把所有使用 `px_net_plugin` 的网络插件作为同一
兼容单元增量重编译并发布，避免新 Render 与旧插件混用结构布局。build tree 与
`build_official\dist` 的 SHA-256 均一致：

- `px_render.exe`：`B496790EA171C87D3C4FD3D6D1D594AD4621098ADAEFE0CA85D37A66D054B423`；
- `net_relay.dll`：`3D8E0E925338A530C2AE301C47BDE5A87E25C733948551613A39362FA0743396`；
- `net_ws.dll`：`A07F69EBB81248640F4EE8D43FDD32FE4DC5555488A39744C8AA0F5F6C0404E3`；
- `net_udp.dll`：`BA6C9E20C197753E4DC186DE52CF0337A95E04A9E2319F8AA765BF5BF07DF790`；
- `net_rtc.dll`：`72A08A5491781932499CF1AF91D0AD64726AFCC851DB315BC1A6A4C5E1ED345E`；
- `net_rtc_local.dll`：`564F958BDF73E4673B5EE42D8A84843597259E47B6B104AFDC3FDC6EC4A72889`。

### 12.28 Phase 7 MiniAudio 默认设备重建生命周期收敛

桌面系统音频使用 MiniAudio/WASAPI loopback。默认播放设备 reroute 或 interruption 恢复时，
原实现从第三方通知回调启动一条 detached 线程，固定 sleep 80 ms 后重建采集设备。即使采集
已经 Stop，该线程和 DLL 中的函数体仍可能继续运行，插件关闭无法证明延迟任务已经收敛。

本批保留 miniaudio 的第三方 C 回调和设备结构，不修改其源码或 ABI，只调整项目维护的
`MiniAudioCapture`：

- 每个活动 capture 拥有一条 `jthread` 重建 worker；worker 捕获共享状态和 owner 的
  `weak_ptr`，不捕获裸 `this`；
- 80 ms 离开通知回调窗口改为 stop-token 可取消等待；重复 reroute 在已有 pending 请求时
  合并，不创建额外线程；
- `Stop()` 先取消并 join worker，再释放 WASAPI device/context，因此 Stop 返回即是 DLL
  延迟代码的生命周期屏障；再次 Start 会创建新的 worker，支持重复启停；
- 重建 reason 改为 `std::string` 值语义，不在项目异步接口中传递临时 C 字符指针；
- 测试构建提供受宏保护的触发和计数入口，生产 DLL 不暴露额外测试 API。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| 真实音频重建 | 本机 Realtek 默认扬声器，播放 880 Hz 测试音期间主动重建；重建后继续输出 48 kHz / 2 ch / 16 bit PCM，峰值 8191，非静音 |
| 完整重建稳定性 | 启动、触发重建、继续采集、停止连续 10/10 PASS |
| Stop 取消 | 测试程序内 10 轮在 80 ms pending delay 内立即 Stop；每轮均在 500 ms 内返回，successful reinit 保持 0 |
| focused build | `cap_was_audio`、`test_plugin_was_audio_capture`、`test_miniaudio_reinit_cancel` PASS；未运行 `build_official.bat`，未编译 Rust/Web |
| ownership / whitespace | `check_cpp_ownership.ps1`、`git diff --check` PASS；项目代码无新增裸指针、manual ownership、detached thread 或 `[this]` 捕获 |
| dist smoke | build tree 与 dist 插件哈希一致；`px_service` Running，dist Render 已实际加载新版 `cap_was_audio.dll`，原 Relay 媒体/文件双通道仍 Established |

`cap_was_audio.dll` 的 build tree 与 `build_official\dist` SHA-256 一致：
`1875E2C7BAD9BD4B5F0D5A998DE3CDB95AC296EB19680749A930488966405FDB`。

### 12.29 Phase 7 WAS 音频外层插件生命周期收敛

12.28 已经关闭 MiniAudio 内部设备重建的 detached worker，但插件外层仍在
`WasAudioCapturePlugin` 上直接保存采集状态和重启线程，重启 worker、格式/数据/
停止回调均捕获插件 `this`，致命停止判断还保留了 `IAudioCapture*`。这使
`OnDestroy`/DLL 卸载对排队回调的失效和 worker 退出缺少可独立验证的屏障。

本批保持 `GetInstance` 插件单例、加载/卸载 ABI 和 WebRTC 结构不变，完成以下收敛：

- 新增 `WasAudioCaptureRuntime`，采集实例、格式状态、PID、重试 generation、事件通道
  和 owned `jthread` 全部归共享 Runtime 所有；插件对象只同步转发生命周期方法；
- 格式、PCM、分声道和 stop 回调只捕获 `weak_ptr`；致命停止判断使用
  `weak_ptr<IAudioCapture>::lock()`，不再保存或捕获裸指针；
- Stop、PID 变更、外部 Start 和 Shutdown 都递增 generation 并取消 pending retry；
  旧 capture 的迟到 stop 回调不能复活新会话；
- `Shutdown` 先禁用事件通道，再取消 worker、停止 capture 并 join；返回后旧 PCM/
  stop 回调即使被人工触发也不能再访问插件或发布事件；
- 新增 `build_cpp_was_audio_tests.bat`，只增量编译 WAS 音频插件及相关 C++
  测试，不运行 `build_official.bat`、Rust 或 Web 构建。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| Runtime 致命停止 | 进程存活时自动重启 10/10 PASS；进程已退出时正确放弃 |
| pending 取消 | 致命停止后立即 Stop，延迟窗口内取消 10/10 PASS，无取消后重启 |
| 回调/销毁 | 事件回调内 Stop 10/10 PASS；Shutdown 后触发旧 PCM/致命 stop 回调 10/10 无投递、无崩溃 |
| 重复启停 | 同一 Runtime 连续 Start/Stop 10/10 PASS |
| 真实音频 | 本机 Realtek 默认扬声器，880 Hz 回环采集和主动设备重建 10/10 PASS；48 kHz / 2 ch / 16 bit，峰值 8191 |
| DLL 真实生命周期 | 10 轮 LoadLibrary / OnCreate / Start / Stop / OnStop / OnDestroy / FreeLibrary PASS；每轮模块句柄归零 |
| focused build | `cap_was_audio`、`test_was_audio_capture_runtime`、`test_was_audio_plugin_dll_lifecycle`、`test_plugin_was_audio_capture`、`test_miniaudio_reinit_cancel` PASS |
| ownership / whitespace | `check_cpp_ownership.ps1`、`git diff --check` PASS；新 Runtime 和测试无新增裸指针、`[this]` 或 detached thread；DLL 测试中的指针仅为已标注的插件 ABI 边界 |
| dist smoke | 新 DLL 已发布到 dist；`px_service` Running，dist Render 已实际加载该 DLL，Relay 媒体/文件两条 30502 连接 Established |

`cap_was_audio.dll` 的 build tree 与 `build_official\dist` SHA-256 一致：
`6B0F400DDA9FC3AF9E6D7BB6D4DC181EC28832792CC158AC9F50EB5A6800F20C`。

### 12.30 Phase 7 Client/Panel 延迟任务与退出链路收敛

客户端切换显示器后的二次桌面刷新原先占用 context worker `sleep_for(200ms)`；
`BaseWorkspace::Exit` 在同步清理前启动一条 500 ms detached 强杀线程，但清理后又会
立即执行同一强杀，因此前者不提供额外完成保证，反而是不可回收的重复退出路径。

Panel 的“退出所有程序/卸载”路径更复杂：Qt UI 线程先 sleep 500 ms，再启动
detached 线程依次 sleep 800 ms、启动 service helper、sleep 1500 ms 并强制清理进程。
这会卡住 UI，而且 Panel 自身无法验证 detached 阶段的顺序、失败兜底和对象失效。

本批完成以下收敛：

- `ClientContext` 新增 `PostDelayTask`，由 Qt 可取消 timer 计时后投递回 client task
  lane；切屏的 200 ms 刷新不再占用 worker sleep，且 Context Exit 后自动失效；
- 删除 `BaseWorkspace::Exit` 中重复的 detached 强杀 worker，保留原有“完成插件/连接/
  Context 清理后立即结束当前客户端”语义；
- 新增 `PanelShutdownSequence`，将 500/800/1500 ms 三阶段改为 UI/worker scheduler
  上的明确状态链；回调仅通过 `weak_ptr`、`shared_ptr` 和 `QPointer` 边界访问状态；
- `ServiceManager::ShutdownDetached` 返回 helper 是否成功启动；启动成功时保留 1.5s
  兜底窗口，启动失败时立即进入本地清理；
- 新增 `build_cpp_panel_shutdown_tests.bat`，只编译 Panel 关闭测试和 `px_panel`，
  不运行发版整编。

专项验收结果：

| 门禁 | 结果 |
| --- | --- |
| Panel 关闭状态链 | 4 tests 连续 10 轮，40/40 PASS |
| 顺序/时序 | prepare 500 ms → helper 800 ms → 成功后 fallback 1500 ms；重复 Start 幂等 |
| 故障覆盖 | helper 启动失败的 0 ms 兜底、排队时 owner 销毁、prepare/helper 抛异常后继续兜底，全部 PASS |
| focused build | `px_client`、客户端三个运行时插件、`px_rtc_client`、`px_panel`、`test_panel_shutdown_sequence` PASS；未运行 `build_official.bat` |
| ownership / whitespace | `check_cpp_ownership.ps1`、`git diff --check` PASS；本批无新增裸指针、`[this]`、detached thread 或阻塞 sleep |
| dist smoke | 最终 Client/Panel 已发布到 dist 并启动 Panel；`px_service` Running，dist Render 存活，Relay 媒体/文件两条 30502 连接 Established |

最终 build tree 与 `build_official\dist` SHA-256 一致：

- `px_client.exe`：`E037898363A7A08DF76C0C6D8F24822C321017507ACB8C90F205953DC5FBDBE2`；
- `clipboard.dll`：`D392EDC069ACA24237B670806D357E1125FF20CB5AABA813C69E34FAE5CE43D9`；
- `px_panel.exe`：`C22C36251C51EE56EAD7DCF892FE286B306177AEE12E115C2E1D2643AFB1FDD8`。
