# C++ 裸指针与 `px_common` 生命周期审计（2026-08-25）

> 历史基线说明（2026-08-26 更新）：本文第 1 至 8 节记录的是迁移实施前的审计快照，
> 用于保留风险来源和整改依据，不代表当前 Asio Notify 交付状态。此次变更范围内的
> MessageNotifier/asio2 并发访问、Client/Panel/Render/SDK/encoder/relay/Android native
> 监听生命周期，以及 `Thread`、`Data`、`File`、`SharedPreference` 基础问题已经整改并
> 通过析构时仍有排队回调、派发中注销、回调内停止和重复启停测试。当前结果见
> `docs/asio_notify_concurrency_acceptance_report_20260826.md`。
>
> 这不等于宣称整个历史仓库的所有裸指针债务已经清零。未触及的旧代码仍按本文库存
> 分批治理；所有新代码及变更范围内旧代码受
> `docs/cpp_smart_pointer_standard.md` 和所有权检查器硬门禁约束。WebRTC 借用 ABI、
> 插件实例边界及不维护的第三方源码继续按明确例外处理。

## 1. 结论

当前项目尚未达到“自研 C++ 全面禁止裸指针状态和异步裸 `this`”的目标，整体风险为高。
主要风险不在 Qt 控件的同步父子关系，而在 Client、Render、Panel 的消息监听、线程、网络回调和任务队列仍大量捕获裸 `this`。对象退出、窗口销毁、断线重连或队列延迟执行时，存在真实的 use-after-free、竞态和 `std::terminate` 条件。

本次审计排除了普通第三方源码、`src/px_deps/px_webrtc_client` 的 libwebrtc ABI/Observer 指针、插件 `GetInstance`/实例/加载器 ABI 边界以及生成代码。插件实例裸指针不计入问题，但项目代码注册到插件或其他执行器中的裸 `this` 回调仍在检查范围内。

## 2. 静态库存

扫描约 1314 个项目 C++ 文件。以下是语法候选数，不等同于确定缺陷数：

| 类型 | 候选数 | 说明 |
|---|---:|---|
| 显式捕获 `this` | 914 | 包含同步与异步 lambda |
| 明显异步裸 `this` | 498 | 同行出现 Post、Listen、thread、Callback 等调用 |
| 可能隐式捕获 `this` 的 `[=]` | 111 | 现有检查器没有覆盖 |
| 疑似裸指针成员 | 532 | 包含 Qt 子对象、C API 句柄、借用对象和插件 ABI |
| 裸指针容器 | 82 | 需要逐项区分所有权与插件借用边界 |
| 手工 `delete` | 51 | 已排除主要厂商源码 |
| 手工 `new` | 1564 | 大量是 Qt 父对象接管，不能直接按缺陷计数 |

## 3. 已确认的高风险位置

### 3.1 Client Workspace

`src/px_client/ct_base_workspace.cpp` 的析构函数为空，但对象注册了大量捕获裸 `this` 的 MessageNotifier、SDK 和二次队列回调。Listener 析构可以使尚未开始的回调失效，但无法保护已经开始执行的裸 `this` 回调。

代表位置：

- `ct_base_workspace.cpp:267`：消息监听捕获 `this`。
- `ct_base_workspace.cpp:490`：空析构函数。
- `ct_base_workspace.cpp:496`：SDK 音频回调捕获 `this`。
- `ct_base_workspace.cpp:503`：再次投递到 UI 队列。

### 3.2 Render Application

`src/px_render/rd_app.cpp:1534` 创建 detached thread 并捕获裸 `this`，延迟 400 ms 调用 `Exit()`；析构函数没有等待该线程。`rd_app.cpp:1782` 的全局控制台处理器也长期保存裸 `this`。

### 3.3 EncoderThread

`src/px_render/app/encoder_thread.cpp` 把裸 `this` 投递到编码线程和插件任务线程。`Exit()` 只停止 `enc_thread_`，无法证明已经进入其他 Context/Plugin 队列的任务全部结束。类使用默认析构函数，没有统一的失效令牌或 drain 屏障。

### 3.4 `px::Thread`

`src/px_deps/px_common/thread.cpp` 的工作线程捕获裸 `this`。当前实现处理了工作线程内部调用 `Exit()` 时不能 join 自己的问题，但没有覆盖“最后一个 `shared_ptr<Thread>` 在工作线程回调中释放”的情况；此时仍可能销毁 joinable `std::thread` 并触发 `std::terminate`。

### 3.5 `Data` 与 `File`

`Data` 自有 `char*` 并使用 `malloc/free`，`File` 自有 `FILE*`，两者都没有显式禁止复制。发生值复制时会形成浅复制和双重释放。`Data::DataAddr() const` 还会从 const 对象返回可写指针。

建议：

- `Data` 改为 `std::vector<std::byte>` 或 `std::vector<char>`。
- `File` 使用带 `fclose` deleter 的 `std::unique_ptr`，并明确删除复制操作。

### 3.6 Clipboard 消息线程

`WinMessageLoop` 持有真实 `std::thread`，析构函数为空，线程入口通过 `std::bind(..., this)` 保存裸对象。`ClipboardManager` 还向消息线程投递捕获裸 `this` 的任务。如果任一路径漏掉显式 `Stop()`，会发生 UAF 或销毁 joinable thread 导致 `std::terminate`。

## 4. `px_common` 评估

### 4.1 可保留的实现

`MessageNotifier` 是当前生命周期设计最完整的公共组件：

- Core 使用 `shared_ptr`。
- Listener、Registration 和 Executor 任务使用 `weak_ptr` 失效检查。
- 支持 Drain、Cancel、回调内 Stop、并发注销、队列上限、异常隔离和统计。
- 19 项专项测试通过。

`WsServer` 也已使用 `weak_from_this()` 保护 asio2 网络回调，方向正确。

### 4.2 需要整改的实现

- `SharedPreference` 手工持有 LevelDB 指针；`Visit()` 持锁调用外部回调，回调重入 Get/Put 会自死锁；`Release() const` 使用 `const_cast` 修改状态。
- `ConcurrentType::WithLock()` 返回 `decltype(auto)`，允许内部引用逃逸到锁外。
- `ConcurrentHashMap::VisitAll()` 采用复制、解锁、回写模式，可能覆盖并发写入；`QueryRange()` 未正确验证负数和反向区间。
- `ConcurrentVector::Visit()` 持锁调用外部回调，存在重入死锁风险。
- Dump Helper 使用全局裸 `ExceptionHandler*`，重复安装泄漏；Breakpad 长期保留调用方传入的 `BreakpadContext*`，生命周期没有被接口表达。
- HttpClient 对象的 headers 和 `req_path_` 可变但没有并发保护；普通 HTTPS 与 Download 的证书验证行为不一致。

## 5. asio2 `event_dispatcher` 风险

维护中的 `asio2::event_dispatcher` 在锁内找到 map 元素后返回裸指针，锁释放后 `direct_dispatch()` 再使用该指针。如果另一线程同时删除最后一个 Listener，map 元素可能被擦除并形成悬空指针。

当前 `MessageNotifier` 把注册、注销和派发串行化到自己的 worker，因此当前唯一使用路径规避了该问题；但不能把 `event_dispatcher` 自身认定为并发安全，现有 asio2 测试也没有覆盖并发 dispatch/remove。

## 6. 检查器评估

`scripts/check_cpp_ownership.ps1` 目前只能作为新增代码的最低门禁，不能作为全项目验收工具：

- 不检测 `[=]` 隐式捕获 `this`。
- 不检测 `std::bind(..., this)`、`malloc/free`、指针容器和复杂裸成员。
- `-ReportAll` 不检查裸成员，并把注释中的 `new/delete` 当成命中。
- `-ReportAll` 会扫描 asio2 内嵌的 standalone Asio/BHO，产生大量噪声。
- 插件 ABI 例外是零散硬编码，不是统一、可审计的例外清单。

## 7. 测试状态与缺口

已运行：

- `test_message_notifier.exe`：19/19 通过。
- `test_common.exe`：22/22 通过。

仍需补充：

- 回调内释放最后一个 Thread owner。
- 对象析构时队列中仍有任务。
- 回调执行中并发注销和析构 owner。
- Data/File 复制与移动语义。
- SharedPreference Visit 回调重入。
- ConcurrentHashMap 并发修改与 Visit 回写冲突。
- WinMessageLoop 未显式 Stop、重复 Start/Stop、线程内 Stop。
- WsServer 销毁时仍有连接和网络回调。
- asio2 dispatcher 并发 dispatch/remove。
- Client/Render/Panel 连续连接、退出、重连期间的对象销毁。

普通开发回归和完整稳定性验收均运行 10 轮；原 100 轮要求已于 2026-08-26 取消。

## 8. 建议整改顺序

1. 完善所有权检查器和历史基线，精确列出第三方、WebRTC、插件 ABI 例外。
2. 修复 `Thread`、`Data`、`File`、`SharedPreference` 等公共基础设施。
3. 修复 Client 的 BaseWorkspace、PxRenderView、Clipboard 和 Context 队列。
4. 修复 Render 的 RdApplication、EncoderThread、AppManager 和网络服务器。
5. 修复 Panel Workspace、Panel Server 和后台任务。
6. Qt 父对象托管字段逐步改成 `QPointer`；控制器和异步对象使用 `shared_ptr/weak_ptr`。
7. 最后处理低风险同步借用及 C/Windows/FFmpeg 句柄的 typed deleter。

不应对整个仓库进行机械替换；每个批次都必须同时补齐退出、回调排队、并发注销、重复启动/停止和 10 轮稳定性测试。
