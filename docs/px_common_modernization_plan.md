# px_common C++23、异步化与跨平台收敛计划

## 1. 文档状态

- 状态：代码实施完成，等待用户统一验收；iOS/Android 设备运行属于外部平台验收门禁
- 目标平台：Windows、Android、iOS
- C++ 标准：C++23
- 异步基础：standalone Asio、`PxAsyncRuntime`、`PxAsyncScope`
- 依赖升级基线：standalone Asio 1.38.2（官方 tag `asio-1-38-2`，commit `8806a6803cde7054c3049d3666d3ec36786568c5`）
- 明确非目标：本轮不改变现有 TLS 证书校验产品策略

本文覆盖 `src/px_deps/px_common` 的整体改造，不只处理编译标准。最终目标是让 common 成为职责明确、所有权安全、可取消、可关闭、可测试的跨平台基础设施，而不是继续作为 Windows 工具、网络服务器、第三方适配和通用容器的混合目录。

### 1.1 当前实施状态（2026-09-05）

| 范围 | 状态 | 已完成 | 外部验收项 |
|---|---|---|---|
| 阶段 A：依赖与基线 | 完成 | Asio 1.38.2、版本断言、依赖记录、Windows 全部消费目标和 Android 聚焦目标构建 | iOS AppleClang 构建 |
| 阶段 B：路径与文件 | 完成 | 严格 UTF-8 path codec、UTF-8/长路径 manifest、`PxAsyncFile`、取消/deadline/Unicode 测试、业务阻塞文件调用迁移 | Android/iOS 设备文件沙箱测试 |
| 阶段 C：TaskRuntime | 完成 | bounded blocking executor、Panel/Render/Console 迁移、TaskRuntime 和 `std::any` 返回链删除 | 产品负载下阈值调优 |
| 阶段 D：基础正确性 | 完成 | Data、Image、String、Random、Time、并发容器、UDP/FEC、Win32/COM/OpenSSL/Breakpad RAII 和安全边界整改 | 设备和硬件场景人工验收 |
| 阶段 E：模块拆分 | 完成 | 七个职责 target、兼容聚合 target、死代码删除、WebRTC 聚合头移出 Common、Windows/Android 脚本门禁 | iOS CI 节点 |

“完成”表示仓库内计划代码与可在当前环境执行的自动验证已经收口，不把缺少 Xcode/iOS 工程或移动设备的情况伪装成已验证。
Windows 通过不能替代移动端运行验证；移动端剩余项是发布/设备验收门禁，不是继续保留旧架构的理由。

### 1.2 当前 Windows 验证证据（2026-09-05）

- Common CTest：40/40 通过；包含 runtime、path codec、async file、blocking executor、并发容器、UDP、消息分发和关闭竞态。
- 文件传输：10/10 通过；SDK/Relay WebSocket 重连：2/2 通过。
- Render 生命周期：19/19 通过；Panel shutdown/lifetime：5/5 通过。
- Common、SDK、Panel、Client、Render、`px_render_rtc`、`px_render_rtc_remote` 和 `px_client_rtc` 聚焦构建通过。
- `scripts/check_cpp_ownership.ps1` 通过新增代码的裸指针、手工所有权、异步 `[this]` 捕获、Qt ownership transfer 和 150 列门禁。
- `px_client.exe`：`70515EDFC8E928AC9A4FD056241334A48A226D2FB968F92416B10E30D680070E`。
- `px_client_rtc.dll`：`F2FE941CAAA909D171B678E5F79F70129B9307746627452B46BD1405F5715485`。
- `px_panel.exe`：`C9F1707CF6AD2850F4D95C12CECFA4F6E56738BCE59A3CCF6007277B476838BA`。
- `px_render.exe`：`B13F5F21E8D8EF7F1549466E428BF9C0A7932B7D70F2CAC967C7FCE61BC175E7`。
- `px_render_rtc.dll`：`394483E24C377DAD78D79449122B5463D7B457D0EB6553B0EFD0315299BB163B`。
- `px_render_rtc_remote.dll`：`B286123822C66336BC65420B38E8B13E272ABA0043B5A2EADD8BA3A660B58108`。
- `px_gh.dll`：`84A32D25CD4F263D9C1105181459141A1491FF39A19BD76A5B91726E3A9DA9F0`。
- 上述运行时文件及语言资源均已同步到 `build_official/dist`，发布脚本逐项确认 SHA-256 一致。

### 1.3 移动平台证据与边界（2026-09-05）

- Android NDK arm64-v8a 的 `px_common` 与 `px_sdk` 聚焦构建通过；入口为 `build_cpp_android_common.bat`。
- Android 全应用最终链接仍受既有 Android `RtcClient` 实现缺失阻塞；Common/SDK 静态库已经越过编译和链接门禁。
- 当前 Windows 工作区没有 AppleClang/Xcode 构建环境，也没有可用的 iOS 工程/设备，因此不能声明 iOS 已实机验证。
- iOS 的代码策略仍是标准 C++23、`std::filesystem::path` 原生 POSIX 字节路径和 bounded blocking executor；必须在 macOS CI 补做编译、
  模拟器与真机文件沙箱测试后，发布门禁才算完成。

## 2. 当前基线与主要问题

### 2.1 已具备的能力

- CMake 已要求 C++23。
- 已有 `PxAsyncRuntime`、`PxAsyncScope`、异步延时、异步一次性结果、mailbox、重连监督器和关闭辅助设施。
- Windows 同步文件路径通过 UTF-8 转 UTF-16 和宽字符文件 API 支持中文路径。
- 异步生命周期已有取消、deadline、弱引用和部分关闭测试。

### 2.2 已收敛的问题

- 单一 Common target 已拆为 core、async、file、net、storage、crypto、win 七个职责 target；`px_common` 只保留兼容聚合职责。
- `TaskRuntime`、`ReturnThreadTask` 和 `std::any` 返回链已经删除，异步工作统一到 `PxAsyncRuntime`、`PxAsyncScope` 和
  `PxBlockingExecutor`。
- 同步 `File` 的 EOF、失败推进、64 位偏移和 UTF-8 路径问题已修复；可能阻塞业务线程的文件传输读写已进入 blocking executor。
- `PathFromUtf8`/`PathToUtf8` 统一外部 UTF-8 与原生 `std::filesystem::path` 的转换，Windows 使用严格 UTF-16 边界。
- Data 公开接口改为拥有型对象和 `std::span`；并发容器、UDP/FEC 输入、Image、String、Random、Time 的已知问题已修复并覆盖测试。
- Win32 COM、PDH、进程/线程/token/pipe、桌面、剪贴板、图标、DXGI、FFTW、OpenSSL 和 Breakpad 资源改为 typed RAII 或 smart pointer。
- 所有自动化 Common 测试目标已注册，旧的联网/重复/人工诊断 test executable 已删除；当前 Windows CTest 为 40/40。
  消息总线的 callback-origin drain stop 死锁由测试暴露并已修复。

## 3. 目标架构

### 3.1 目标模块

最终将 `px_common` 兼容聚合目标拆为下列职责目标：

| 目标 | 职责 | 平台 |
|---|---|---|
| `px_common_core` | Data、错误、字符串、时间、路径、小型值类型 | Win/Android/iOS |
| `px_common_async` | runtime、scope、mailbox、取消、deadline、blocking bridge | Win/Android/iOS |
| `px_common_file` | 异步文件、目录遍历、路径 adapter | Win/Android/iOS |
| `px_common_net` | HTTP 边界、重连、连接工作流、网络工具 | Win/Android/iOS |
| `px_common_storage` | LevelDB adapter 和串行化数据库执行 | Win/Android/iOS |
| `px_common_crypto` | 兼容摘要、对称加密 adapter | Win/Android/iOS |
| `px_common_win` | COM、WMI、DXGI、进程、计划任务、防火墙、dump | Windows |

迁移期保留 `px_common` 作为带空聚合翻译单元的兼容静态目标，并公开链接上述职责 target；这样兼容既有链接方式且不再把全部实现编译进单一库。
平台专用库不得从 core 的 PUBLIC 接口传播。兼容名是否删除留给后续 ABI/构建消费方专项迁移，不再阻塞本轮 Common 收敛。

### 3.2 依赖方向

```text
platform applications
        |
        +--> feature modules
                 |
                 +--> common_net / common_storage / common_file
                                      |
                                      +--> common_async
                                               |
                                               +--> common_core

common_win ----> common_core/common_async
```

- core 不依赖 Qt、asio2、CPR、LevelDB、OpenSSL、WebRTC、DirectX 或 Breakpad。
- common_async 只依赖 standalone Asio 公共能力。
- 第三方 API 必须位于 adapter 内，不得把第三方具体类型泄漏到通用业务接口。
- WebRTC 聚合头移出 common，归属 RTC DLL 的共享源码目录。

## 4. 全局 C++ 约束

- 所有项目自有 C++ 对象、成员、局部变量和原子变量确定性初始化。
- 不声明、保存、返回、传递或捕获裸指针。系统和第三方 ABI 只允许瞬时边界值，并立即转入 RAII。
- 异步回调捕获 `weak_ptr`，执行处 `lock()`；不捕获 `this`。
- 文件句柄、线程、timer、registration、COM、OpenSSL、Breakpad 和取消订阅均使用 RAII。
- fallible API 返回 `std::expected` 或项目统一 `PxResult`，不再用空字符串、固定零值或 `nullptr` 混合表达错误。
- 异步函数使用 `PxAwaitable<PxResult<T>>`，不使用 `std::any` 传递结果。
- 协程参数只要可能跨越 initial suspend，就必须按值持有拥有型对象；禁止引用临时 lambda capture、局部变量或调用者 buffer。
- 一行不超过 150 字符，`.clang-format` 为格式权威。
- 不引入 service locator、可变全局单例、通用 `void*`/`std::any` 数据袋或无实际扩展边界的接口层。

## 5. Asio 1.38.2 升级

### 5.1 升级范围

- 替换 `src/px_deps/px_3rdparty/asio2/3rd/asio` 和同级 `asio.hpp` 的 standalone Asio headers。
- 来源固定为官方 tag `asio-1-38-2`，不跟随 master 浮动。
- 保留 asio2 自身源码和公开 API，先验证新 Asio 是否与当前 asio2 兼容。
- 在 `docs/vendored_deps.md` 记录 standalone Asio 的 tag、commit 和来源。

### 5.2 升级门禁

1. `px_common` 增量构建。
2. common 已注册 CTest 全通过。
3. Render network libraries 构建和生命周期测试通过。
4. SDK、Panel、Client、Render 目标依次构建。
5. Android CMake compile gate 通过。
6. 新增编译期断言确认 `ASIO_VERSION == 103802` 或不低于批准基线。
7. 不通过时记录 asio2 兼容差异；不得通过修改大量 asio2 内部实现掩盖不兼容。

## 6. 路径与编码模型

### 6.1 统一规则

- 内部唯一的路径类型为 `std::filesystem::path`。
- Windows 内部保存原生 UTF-16；POSIX 平台保存原生字节路径。
- JSON、网络、配置和日志边界统一使用经过验证的 UTF-8 `std::string`。
- Windows API 直接使用 `path.c_str()` 调用 `W` 版本 API。
- Windows 可执行程序通过 common 的 interface manifest 启用 UTF-8 active code page 和 long-path-aware。
- 该 manifest 只兼容仍使用窄 API 的第三方库；项目自有 Windows 文件代码不得因此退回 `A` 版本 API。
- UTF-8 active code page 以 Windows 10 1903 为最低系统能力；项目现有进程音频能力已要求 Windows 10 2004，因此不降低产品基线。
- 通用接口不接受或返回 `std::wstring`。

### 6.2 新接口

```cpp
PxResult<std::filesystem::path> PathFromUtf8(std::string_view utf8);
PxResult<std::string> PathToUtf8(const std::filesystem::path& path);
```

- Windows 使用严格的 UTF-8/UTF-16 转换并返回具体错误。
- Android/iOS 验证 UTF-8 后构造 native path，不经过 `wstring`。
- `SharedPreference` 接收 `filesystem::path`，数据库名按 UTF-8 严格解码；Windows 已验证 LevelDB 中文和 emoji 路径。
- 删除 `U8S` 的 `char8_t*` reinterpret cast。
- 迁移完成后删除 `U8Path`；迁移期可保留 deprecated 兼容构造，但不得新增调用。
- Android JNI 适配层负责 Modified UTF-8/UTF-16 边界，不能把 `GetStringUTFChars` 结果直接视为标准 UTF-8 文件名。
- Apple adapter 负责 `NSString`/`NSURL` 与 native path 的边界转换。

### 6.3 路径测试

- ASCII、简体中文、日文、emoji、空格、组合字符。
- 非法 UTF-8 必须返回错误。
- Windows 长路径、UNC 路径、中文父目录和文件名。
- create/open/read/write/append/rename/copy/delete 的同步迁移验证。
- Android JNI 含补充平面字符路径。
- Apple NFC/NFD 名称访问，不使用字符串相等判断文件身份。

## 7. 异步文件体系

### 7.1 公共接口

新增不可复制、共享生命周期安全的 `PxAsyncFile`：

```cpp
class PxAsyncFile final {
public:
    static PxAwaitable<PxResult<std::shared_ptr<PxAsyncFile>>> OpenAsync(
        std::shared_ptr<PxAsyncRuntime> runtime,
        std::filesystem::path path,
        PxFileOpenOptions options,
        std::chrono::steady_clock::time_point deadline);

    static PxAwaitable<PxResult<std::shared_ptr<PxByteBuffer>>> ReadAtAsync(
        std::shared_ptr<PxAsyncFile> file,
        std::uint64_t offset,
        std::size_t size,
        std::chrono::steady_clock::time_point deadline);

    static PxAwaitable<PxResult<std::size_t>> WriteAtAsync(
        std::shared_ptr<PxAsyncFile> file,
        std::uint64_t offset,
        std::shared_ptr<const PxByteBuffer> data,
        std::chrono::steady_clock::time_point deadline);

    static PxAwaitable<PxResult<void>> FlushAsync(
        std::shared_ptr<PxAsyncFile> file,
        std::chrono::steady_clock::time_point deadline);

    static PxAwaitable<PxResult<void>> CloseAsync(std::shared_ptr<PxAsyncFile> file);
};
```

异步操作必须拥有 buffer 生命周期，禁止在挂起期间保存调用者的裸指针或临时 `span`。

### 7.2 平台后端

- 已实施方案统一使用 `PxBlockingExecutor` 承接 regular-file 阻塞调用，并通过 Asio coroutine 恢复结果、取消与 deadline 语义。
- 原因是 standalone Asio 没有一个在 Windows、Android、iOS 上行为一致的普通文件异步后端；直接暴露平台 file service 会让公共接口和关闭语义分叉。
- Windows 文件边界使用原生宽路径；Android/iOS 使用 POSIX/native `std::filesystem::path`，均不经过 ANSI code page。
- 后续只有在性能统计证明 blocking bridge 是瓶颈时，才在 adapter 内增加 Windows overlapped I/O、Android io_uring 或 iOS `dispatch_io`；
  公共 `PxAsyncFile` 契约不随平台改变。
- `std::filesystem` 元数据查询和递归遍历视为阻塞操作，在移动端同样进入 blocking executor。

### 7.3 生命周期

- 状态机：`Created -> Opening -> Open -> Closing -> Closed/Failed`。
- Close 停止准入、取消可取消操作、等待已提交操作归还资源。
- Close 幂等；析构不在 runtime 线程上无限等待。
- deadline 到达后不允许迟到成功覆盖 timeout/cancel 结果。
- 同一文件的顺序写和 append 使用 strand；明确 offset 的随机读允许受控并发。

## 8. TaskRuntime 废弃与删除

### 8.1 替代模型

| `TaskRuntime` 能力 | 新能力 |
|---|---|
| 普通任务投递 | `PxAsyncScope::Spawn` |
| 串行网络/状态任务 | 独立 scope/strand |
| 有界队列 | `PxAsyncMailbox` |
| 阻塞任务 | `PxBlockingExecutor` |
| `std::any` 返回 | 模板化 `PxAwaitable<PxResult<T>>` |
| task id 删除 | cancellation handle 或 operation id |
| 手工 Exit/Join | scope stop/drain 和 runtime RAII |

### 8.2 Blocking executor

协程不能让同步文件、LevelDB、CPR 或第三方阻塞调用自动变成非阻塞。`PxAsyncRuntime` 增加专用 bounded blocking executor：

- 独立于 control/state/network executor。
- 固定并发度和有界等待队列。
- 返回 `QueueFull`、`ServiceStopped`、`Timeout`、`Cancelled` 等结构化错误。
- 类型安全地传播返回值和异常。
- 支持 drain/cancel 两种关闭模式。
- 记录等待、执行、超时、拒绝和高水位统计。

### 8.3 迁移顺序

1. Panel Console 的录像扫描、文件读取和同步上传桥接。
2. Panel `PxContext::PostTask/PostNetworkTask/PostDBTask`。
3. Render `RdContext::PostTask`。
4. 删除 `ReturnThreadTask` 和 `std::any` 回调接口。
5. 调用点归零后删除 `task_runtime.h/.cpp` 及 CMake source。
6. 删除只服务于 TaskRuntime 的 Snowflake task id 使用。

`Thread` 不与 TaskRuntime 同批强制删除。视频解码、音频、D3D、编码器和 WebRTC 线程亲和场景逐项评估，最终改为命名明确的专用 executor 或 `std::jthread`。

## 9. 基础类型整改

### 9.1 Data 和 Image

- `Data` 使用值类型或共享所有权 buffer，不公开裸地址作为长期接口。
- 读视图使用同步作用域内的 `std::span<const std::byte>`；异步边界传递拥有数据的对象。
- const 对象不得返回可写数据。
- Image 所有尺寸和格式确定性初始化。
- stb 解码内存使用自定义 deleter 的 `unique_ptr`，复制后立即释放。
- 压缩解码失败返回 `PxResult`，不返回半初始化对象。

### 9.2 字符串、时间和随机数

- Split/Replace 明确拒绝空 delimiter/from。
- `std::isdigit` 等接口统一转换到 `unsigned char`。
- 时间使用 `system_clock` 表达墙钟、`steady_clock` 表达 deadline/耗时。
- 平台本地时间转换使用线程安全 API 或受支持的 chrono 格式化。
- 随机数使用 thread-local engine 和标准 distribution，删除 `rand()`。

### 9.3 并发容器

- 新代码禁止使用通用 `ConcurrentType/Vector/Queue/HashMap`。
- 缺失值返回 `optional/expected`，不返回 `V{}`。
- 不在持锁期间调用任意外部函数。
- 不允许 snapshot 回写覆盖并发更新。
- 队列提供原子的 push/try-pop/close，异步队列统一使用 mailbox。
- 调用点迁移后删除不再使用的通用并发容器。

### 9.4 Win32 与第三方资源

- 修复 MonitorWinInfo 句柄自赋值。
- COM 接口使用 `ComPtr` 或项目 RAII wrapper。
- PDH、HANDLE、HMODULE、GlobalLock、OpenSSL context、Breakpad handler 均明确拥有者。
- crash dump 移至 Windows-only target，不向移动平台声明无实现接口。

## 10. 无用、过期与错位代码处理

### 10.1 已删除的无消费者实现

- `frame_common.h`
- common 旧 `ws_server.h/.cpp`
- `qwidget_helper.h/.cpp`
- `math_helper.h/.cpp`
- 未使用的 `win32/dynamic_library.h/.cpp`
- 只有 include 的 `monitors.cpp`
- 固定返回 0 且无调用的 `HttpClient::HeadFileSize`

以上文件均已完成全仓引用扫描后删除，并由 Common、Panel、Client、Render 和 RTC 目标链接验证。

### 10.2 迁移后删除

- `task_runtime.h/.cpp`（已删除）
- `ReturnThreadTask` 和 `std::any` 返回回调（已删除）；专用线程场景的 `ThreadTask`/`SimpleThreadTask` 继续逐项迁移
- `defer.h` 已删除，项目使用明确的 typed scope-exit RAII helper。
- `const_auto.h` 的 `cat/cexpr` 宏已删除。
- `U8Path/U8S` 已删除，统一使用 path codec。
- `gd_md5` 重复实现已删除，兼容 MD5 使用 OpenSSL EVP RAII adapter。
- 通用并发容器保留为兼容 API，但已经修正原子操作、缺失值、锁内回调和 snapshot 覆盖问题；新异步队列优先使用 mailbox。

### 10.3 移出 common

- `webrtc_helper.h` 已从 Common 移至 `px_webrtc_client` RTC 适配目录，Render 两个 RTC DLL 与 Client RTC DLL 共同引用。
- QR/VDF 等第三方代码标记为 vendored implementation，不纳入项目自有格式和所有权机械整改。
- DirectX、WMI、dump、计划任务、防火墙放入 `px_common_win`。

### 10.4 明确保留的边界

- QR、VDF、Base64 implementation、Reed-Solomon C 实现和 `process_cmdline` 的外部/ABI 风格代码不做机械所有权重写。
- `src/px_deps/px_webrtc_client` 继续遵守 libwebrtc borrowed observer/track/SDP ABI，Common 收敛不改变这套外部生命周期合同。
- Windows IP Helper 的链表节点只是 vector-owned API 缓冲区视图，使用逐行审核注解，不把 borrowed 节点存入对象状态或异步捕获。
- 迁移期间发现的旧 Android `.cxx` 生成树备份已移出仓库到
  `D:\GoCloud\GammaRayPremium_generated_backups\android_app_cxx_stale_common_migration`，不再参与源码扫描或构建发现。

## 11. 日志和性能统计

### 11.1 错误日志

- 错误日志包含稳定事件码、stage、operation id、平台错误码和可恢复性。
- 不记录密码、token、完整 query、剪贴板正文或用户文件正文。
- 路径日志使用统一 UTF-8 转换；转换失败时记录结构化错误，不回退到 ANSI。
- 同一持续故障按状态转换记录，不在高频循环逐包刷日志。

### 11.2 关键指标

- runtime：活跃任务、拒绝、失败、取消、关闭耗时。
- blocking executor：队列深度、高水位、排队延迟、执行耗时、超时和拒绝。
- async file：打开数、并发 I/O、字节数、吞吐、P50/P95/P99 延迟、短读写、错误码分布。
- mailbox：准入、满队列拒绝、关闭丢弃、消费者滞后。
- UDP：非法包、超限 shard、FEC 成功/失败、组帧超时和内存上限拒绝。

统计默认聚合后周期输出，单操作只在失败或超过慢操作阈值时记录。

### 11.3 已实施的日志与统计落点

- `PxAsyncScopeStatistics` 暴露 spawned/completed/failed/rejected/outstanding，网络关闭路径在 drain 失败或残留任务时记录稳定错误码、
  retryable、stage 和 outstanding，不在正常包循环刷日志。
- `PxBlockingExecutorStatistics` 暴露 submitted/completed/failed/rejected/cancelled、active/pending、高水位、总计与最大排队/执行时间。
- `PxAsyncFileStatistics` 暴露读写次数、字节、失败、超时和 active operation；错误继续通过 `PxResult` 交给最了解业务 operation id 的调用方记录，
  避免 Common 与业务层重复报同一次失败。
- `MessageNotifier` 统计 accepted/dispatched/rejected/coalesced/callback exceptions、总队列和各 lane 高水位；队列满、执行器异常、listener 异常记录错误，
  超过阈值的 listener 记录一次慢回调告警。
- 文件传输 session、重连 supervisor 和 transport shutdown 使用同一原则：正常状态转移为 debug/info，最终失败为 error，拥塞/慢操作为聚合 warning。
- 当前统计结构提供累计值、最大值和高水位；P50/P95/P99 需要部署侧有界直方图/telemetry sink，不能用每操作日志计算，也不在 Common 内私自新增上报后端。

## 12. 测试计划

### 12.1 单元测试

- 路径和编码的全字符集往返及非法输入。
- 文件 EOF、短读写、超过 2 GiB offset、取消、deadline、关闭竞态。
- runtime 重复 start/stop、部分启动失败、任务内部 shutdown。
- scope 销毁时仍有 queued callback、取消后迟到完成、不同 lane 关闭。
- blocking executor 满队列、任务抛异常、取消等待、运行中取消、drain/cancel。
- Data/Image 空输入、溢出、解码失败和资源释放。
- UDP 极端长度、shard 上限、乱序、重复、伪造包和 fuzz。

### 12.2 生命周期测试

- 有 queued callback 时销毁 owner。
- dispatch 过程中 unregister。
- callback 内触发 shutdown。
- 反复 start/stop 100 次。
- 文件正在读写时 Close/析构。
- runtime 先停、operation 后完成。

### 12.3 平台门禁

- Windows：MSVC C++23 构建、common CTest、中文/emoji/长路径运行测试。
- Android：NDK C++23 编译、设备/API 26 基线运行测试、JNI emoji 路径测试。
- iOS：AppleClang C++23 编译、模拟器和真机文件沙箱测试。
- sanitizer：可用平台运行 ASan/UBSan；Windows 增加 Application Verifier 或等价句柄检查。
- 关键 parser 和路径转换增加 fuzz target。

所有 common 测试必须通过 `add_test` 注册，不能只生成 executable。

### 12.4 本轮执行记录（2026-09-05）

| 门禁 | 结果 | 覆盖重点 |
|---|---:|---|
| Common CTest | 40/40 | runtime、UTF-8 path、同步/异步文件、blocking executor、并发容器、UDP、消息总线、取消/timeout/close |
| 文件传输 CTest | 10/10 | 路径安全、压缩、job、两阶段发送、session、完整性、terminal、SDK transport E2E |
| SDK/Relay 重连 | 2/2 | 手工重连监督、退避、连接代际、关闭 |
| Render lifecycle | 19/19 | queued callback、owner 销毁、RTC/UDP/WS/Relay、音频 capture、execution context |
| Hook capture lifecycle | 2/2 | hook audio worker、WS IPC client |
| Render architecture quick | 16/16 | 14 个 unit + 2 个 architecture guard，异步生命周期扫描、日志隐私扫描和 artifact hash 通过 |
| Panel shutdown/lifetime | 5/5 | shutdown 顺序、running pipe、auth、Win message window、Qt lifetime guard |
| Android NDK arm64-v8a | 构建通过 | `px_common`、`px_sdk` |
| C++ quality gate | 通过 | 新裸指针、manual ownership、`[this]`、Qt transfer、150 列 |

测试和最终目录审查过程中实际发现并修复四项问题：

1. `MessageNotifier::Stop()` 从 runtime callback 内调用时同步 `Join()`，而 work guard 尚未释放，形成自等待；现在 callback-origin stop 只发起
   drain，完成回调负责释放 guard，外部 Stop/析构再 join。
2. WASAPI 帧数字节数从 Windows `LONG` 隐式窄化为 `span::size_type`；现在先转换为无符号 `size_t` 并在 Win32 写入边界显式转换。
3. 旧进程命令行读取仍使用 `malloc/free`、可写入的 `LPCWSTR` 输出参数和 ANSI 转换；现在收敛为拥有型
   `std::optional<std::wstring>` 返回值，并以值初始化和同步 Win32 边界完成远程进程读取。
4. 进程测试辅助程序的 UTF-16 转 UTF-8 逻辑把结尾零字符写入少分配一个字节的 `std::string`；现在按显式输入长度分配并校验转换结果。

发布前仍必须补做：Android 设备 JNI/文件沙箱运行、macOS 上 AppleClang iOS 编译及模拟器/真机测试、产品硬件和网络环境的人工验收。
Render quick 的机器可读证据位于 `test-results/render-architecture/20260905-160428-quick`。

## 13. 实施阶段与完成定义

### 阶段 A：依赖与基线

- 已完成本文档、Asio 1.38.2、版本断言和依赖记录。
- 已通过 Windows Common、network、SDK、Panel、Client、Render 聚焦增量构建。

### 阶段 B：路径和异步文件

- 已引入统一 path codec 和跨平台 `PxAsyncFile` blocking bridge。
- 已迁移文件传输等高价值阻塞调用，并修复同步 File 正确性。
- 旧同步接口暂作兼容 adapter；新异步业务不得新增直接阻塞调用。

### 阶段 C：TaskRuntime 删除

- 已引入 bounded blocking executor，迁移 Panel Console、Panel Context、Render Context。
- 已删除 TaskRuntime、返回回调和旧测试，补充协程、取消与 callback-origin shutdown 测试。

### 阶段 D：基础正确性和所有权

- 已完成 Data/Image/String/Time/Random/并发容器整改。
- 已完成本轮涉及的 Win32、COM、OpenSSL、Breakpad 资源 RAII 化。
- 已完成 UDP 输入上限、span/buffer 所有权和 FEC codec RAII 整改。

### 阶段 E：模块拆分和清理

- 已拆分 CMake targets 和依赖方向，保留兼容聚合 target。
- 已删除无用代码并把 WebRTC helper 迁入 RTC 适配目录。
- 已建立 Windows 和 Android 聚焦脚本门禁；iOS 门禁定义完成，执行依赖 macOS/Xcode CI。
- 已更新架构、依赖、日志、测试和验收记录。

### 完成定义与验收分界

- `TaskRuntime` 及其类型从生产代码和构建系统删除。
- 新文件业务只使用 `PxAsyncFile` 或明确标注的同步 adapter。
- common 自有代码通过裸指针、未初始化成员和 150 列门禁。
- Windows core/async/file 已编译并运行测试，Android Common/SDK 已通过 NDK 编译；iOS 编译和两端设备运行是发布外部门禁。
- 所有异步资源支持取消、deadline、幂等关闭和安全析构。
- 无敏感日志，无高频逐操作性能日志。
- Windows 客户端相关产物已同步到 `build_official/dist` 并校验 SHA-256，可交付用户统一验收。

## 14. 提交策略

- 每个阶段独立提交，依赖升级与业务改造不混在同一提交。
- 每次提交写明已运行的目标和测试。
- 不运行 `build_official.bat`，除非用户明确要求发布级全量构建。
- 保留用户现有未跟踪文件和无关修改，不纳入提交。

## 15. 外部依据

- [Standalone Asio 1.38.2 与支持平台](https://think-async.com/Asio/index.html)。
- [Microsoft：Win32 进程 UTF-8 code page](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)。
- [LevelDB Windows Env 主线实现](https://github.com/google/leveldb/blob/main/util/env_windows.cc)。
