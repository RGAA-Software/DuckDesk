# Crash 风险分析清单

本文基于当前代码的静态分析整理。由于这是一个 C++/Qt/插件/多线程/DirectX/Vulkan/WebRTC 混合工程，严格意义上无法只靠静态阅读列出“数学意义上的所有 crash”。下面列的是当前代码中有明确证据、触发条件可推导、或同类工程中高概率发生的 crash 风险。

## 结论摘要

最高优先级风险集中在这些位置：

1. 插件和皮肤动态加载：DLL ABI、导出符号、类型强转、插件生命周期。
2. 异步回调捕获 `this`：对象销毁后任务或网络回调仍执行。
3. 渲染端采集/编码链：DDA/GDI 切换、D3D 设备、共享纹理、编码器插件。
4. 文件传输 UI：`QModelIndex`、行号越界、持久编辑器状态。
5. Protobuf/网络消息：解析失败处理不一致，部分发送路径缺空指针保护。
6. GPU/媒体栈：Vulkan/libplacebo/FFmpeg/NVENC/AMF 对空资源或非法格式敏感。
7. 手写内存复制和固定数组：`memcpy` 长度未统一校验。

## 1. 插件 DLL 缺失、损坏或 ABI 不匹配

位置：

- `src/GammaRay/src/render/plugins/plugin_manager.cpp:66`
- `src/GammaRay/src/client/plugins/ct_plugin_manager.cpp:48`
- `src/GammaRay/src/skin/skin_loader.cpp:55`

触发条件：

- `gr_plugins` 或 `gr_plugins_client` 下 DLL 文件存在但依赖 DLL 缺失。
- DLL 编译配置和主程序 ABI 不一致。
- DLL 导出的 `GetInstance` 签名不匹配。
- 插件返回的对象不是预期基类对象。

可能结果：

- `QLibrary::load()` 失败通常只会跳过。
- `resolve("GetInstance")` 返回函数但实际 ABI 不匹配时，调用函数或后续虚函数调用可能直接崩溃。
- `(GrPluginInterface*)func()`、`(ClientPluginInterface*)func()` 是 C 风格强转，没有运行时类型验证。

风险等级：高。

解决方案：

- 定义统一插件 ABI 版本，例如 `GetPluginAbiVersion()` 或在 `GetInstance` 返回对象后立即校验 `plugin->GetAbiVersion()`。
- `QLibrary::load()` 失败时输出 `library->errorString()`，不要只记录路径。
- `GetInstance` 调用外层加 `try/catch (...)`，插件构造失败时隔离为“加载失败”。
- 插件基类增加轻量运行时校验字段，例如 magic number、plugin type、接口版本，校验失败直接跳过。
- 插件目录扫描时只加载白名单文件名或 TOML 中声明的插件，避免误加载无关 DLL。

## 2. 插件导出函数声明不一致

位置：

- `src/render_plugins/nvenc_encoder/nvenc_encoder_plugin.cpp`
- `src/render_plugins/amf_encoder/amf_encoder_plugin.cpp`
- `src/render_plugins/frame_resizer/frame_resizer_plugin.cpp`

触发条件：

- 这些插件实现里能看到 `static void* GetInstance()` 形式，而头文件声明为 `extern "C" __declspec(dllexport) void* GetInstance()`。
- 如果最终符号没有按 C ABI 导出，主程序 `resolve("GetInstance")` 找不到符号，插件被跳过。
- 如果链接/导出行为被其他宏或编译器差异影响，可能出现加载行为不稳定。

可能结果：

- 常见结果是插件不可用。
- 如果调用到错误符号或 ABI 不一致，可能 crash。

风险等级：中高。

解决方案：

- 所有插件实现统一使用一个宏声明导出函数，例如：

```cpp
#define GR_PLUGIN_EXPORT(PluginType) \
extern "C" __declspec(dllexport) void* GetInstance() { \
    static PluginType plugin; \
    return &plugin; \
}
```

- 删除实现文件里的 `static void* GetInstance()` 变体，保证符号名称和 ABI 稳定。
- 在 CI 或构建后增加 `dumpbin /exports plugin_xxx.dll` 检查，确认每个插件都导出未修饰的 `GetInstance`。
- 插件加载失败时把 `plugin_id`、dll path、errorString 写入日志和面板插件页。

## 3. 插件对象为静态对象，释放顺序不可控

位置：

- 多个插件 `GetInstance()` 返回 `static Plugin plugin;` 的地址。
- 插件管理器释放逻辑：`src/GammaRay/src/render/plugins/plugin_manager.cpp:181`
- 客户端插件释放逻辑：`src/GammaRay/src/client/plugins/ct_plugin_manager.cpp:142`

触发条件：

- 主程序持有 DLL 内静态对象裸指针。
- 插件回调仍在执行时 `OnStop()` / `OnDestroy()` 被调用。
- 程序退出时静态对象析构顺序和 Qt/asio 线程退出顺序交错。

可能结果：

- use-after-destroy。
- 插件内部资源已释放，但回调仍访问插件字段。
- 程序退出阶段偶发 crash。

风险等级：高。

解决方案：

- 插件对象继续静态化也可以，但必须引入明确状态机：`Created -> Running -> Stopping -> Destroyed`。
- `OnDestroy()` 内先注销所有回调、停止线程、停止网络，再释放资源。
- 插件管理器释放插件前先设置全局退出标志，后续插件事件回调直接 return。
- 更稳的方案是把 `GetInstance` 改成 `CreatePlugin/DestroyPlugin` 成对导出，由主程序用 `unique_ptr` 管理生命周期；但这会改 ABI，适合集中重构时做。
- 当前阶段建议先做最小修复：保留静态对象，补 `OnStop()` 幂等、`OnDestroy()` 幂等、回调注销和退出标志。

## 4. 插件事件回调捕获 `this`，插件管理器销毁后仍可回调

位置：

- `src/GammaRay/src/render/plugins/plugin_manager.cpp:175`
- `src/GammaRay/src/client/plugins/ct_plugin_manager.cpp:136`

触发条件：

- 插件保存了事件回调。
- `PluginManager` 或 `ClientPluginManager` 已析构或正在释放。
- 插件线程、网络线程、采集线程之后继续触发回调。

可能结果：

- 回调访问已释放的 `evt_router_` 或 `this`。
- 退出时或断线重连时 crash。

风险等级：高。

解决方案：

- `PluginManager` 和 `ClientPluginManager` 改为 `enable_shared_from_this`，注册回调用 `weak_ptr` 捕获。
- 回调里先 `auto self = weak.lock(); if (!self || self->exiting_) return;`。
- 插件接口增加 `ClearEventCallback()`，`ReleaseAllPlugins()` 第一件事先清空插件保存的回调。
- `evt_router_` 使用前判空，并在释放时先置空。
- 对长期运行插件线程，要求 `OnStop()` 必须先停止线程并等待退出，再进入 `OnDestroy()`。

## 5. `PluginManager::On1Second()` 异步投递捕获 `this`

位置：

- `src/GammaRay/src/render/plugins/plugin_manager.cpp:376`
- `src/GammaRay/src/render/plugins/plugin_manager.cpp:377`

触发条件：

- 1 秒定时任务投递到 context 线程。
- 投递后 `PluginManager` 被释放。
- 任务延后执行。

可能结果：

- 访问释放后的 `plugins_`、`context_`。

风险等级：高。

解决方案：

- `On1Second()` 不捕获裸 `this`；改成捕获 `weak_ptr<PluginManager>`。
- 增加 `std::atomic_bool exiting_{false};`，`ReleaseAllPlugins()` 或析构前置 true。
- 投递任务执行时先判断 `context_`、`evt_router_`、`plugins_` 状态。
- 遍历插件前复制一份稳定快照，避免遍历过程中插件集合变化。
- 如果插件集合可能跨线程变更，给 `plugins_` 加 mutex 或只允许在同一插件线程访问。

## 6. 面板/渲染端大量异步任务捕获裸 `this`

位置示例：

- `src/GammaRay/src/render/rd_app.cpp:230`
- `src/GammaRay/src/render/rd_app.cpp:359`
- `src/GammaRay/src/render/rd_app.cpp:913`
- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:79`
- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:85`
- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:91`

触发条件：

- Qt、asio 或自定义 `Thread` 中任务排队。
- 所属对象退出或窗口关闭。
- 任务随后执行。

可能结果：

- 典型 use-after-free。
- 退出、重启渲染端、断网、关闭窗口时更容易触发。

风险等级：高。

解决方案：

- 所有投递到后台线程的 lambda 尽量捕获 `weak_ptr` 或 `QPointer`，不要直接捕获 `this`。
- 非 QObject 的业务对象使用 `std::weak_ptr`；QObject/UI 对象使用 `QPointer<T>`。
- 给核心对象加 `exiting_` 标志，`Exit()` 开始时置 true，回调第一行检查。
- `PostTask`/`PostDelayTask` 增加带 owner token 的重载，owner 失效时自动丢弃任务。
- 对现有代码先从退出路径最容易撞到的类修：`RdApplication`、`WsPanelServer`、`WsPanelClient`、`BaseWorkspace`、`GrWorkspace`。

## 7. 采集插件切换后直接调用 `capture_plugin_->StartCapturing()`

位置：

- `src/GammaRay/src/render/rd_app.cpp:186`
- `src/GammaRay/src/render/rd_app.cpp:368`
- `src/GammaRay/src/render/rd_app.cpp:920`

触发条件：

- DDA 采集失败后切 GDI。
- `SwitchGdiCapture()` 或 `SwitchDdaCapture()` 失败。
- `capture_plugin_` 为空或插件内部未初始化成功。

可能结果：

- 空指针解引用。
- 插件内部状态不完整导致崩溃。

说明：

- `HandleForceGdiEvent()` 中切换后没有再次检查 `capture_plugin_` 是否为空。

风险等级：高。

解决方案：

- 增加统一函数 `StartCurrentCapturePlugin()`，内部完成判空、启用状态、初始化状态、错误日志和返回值处理。
- `SwitchGdiCapture()` / `SwitchDdaCapture()` 返回成功后也不要直接访问 `capture_plugin_`，改为拿局部安全指针。
- `HandleForceGdiEvent()` 中如果切换失败直接 return，并给面板发送错误消息。
- 切换期间设置 `capture_switching_` 标志，禁止并发 Start/Stop。
- `StartCapturing()` 失败时不要连续重试，记录失败原因并降级或停止视频链路。

## 8. DDA/GDI 采集 `StartCapturing()` 与 `SetCaptureMonitor()` 时序竞争

位置：

- `src/render_plugins/gdi_capture/gdi_capture_plugin.cpp:104`
- `src/GammaRay/src/render/plugins/dda_capture/dda_capture_plugin.cpp:371`

证据：

- 代码注释明确提到：`StartCapturing` 后马上 `SetCaptureMonitor` 时 `capture->IsInitSuccess()` 可能为 false。

触发条件：

- 用户快速切换显示器。
- 显示器热插拔。
- 自动从 DDA 切到 GDI 或反向切换。

可能结果：

- 未初始化采集对象被使用。
- 空纹理、空 DC、无效 monitor name 进入后续链路。

风险等级：高。

解决方案：

- 把 `StartCapturing()`、`StopCapturing()`、`SetCaptureMonitor()` 串行化到同一个采集控制线程。
- 采集插件内部增加状态：`Idle / Initializing / Capturing / Stopping / Failed`。
- `SetCaptureMonitor()` 如果遇到 `Initializing`，不要立即操作底层 capture 对象，改为记录 pending monitor，初始化完成后再切。
- 显示器热插拔后先停止采集，刷新 monitor 列表，再启动目标采集。
- 面板端切换显示器 UI 加防抖，短时间多次切换只执行最后一次。

## 9. 编码链 `CopyTexture()` 返回空结果未完全保护

位置：

- `src/GammaRay/src/render/app/encoder_thread.cpp:328`
- `src/GammaRay/src/render/app/encoder_thread.cpp:329`

触发条件：

- `frame_carrier_plugin_->CopyTexture(...)` 自身返回 `nullptr`。
- 共享纹理 handle 无效。
- D3D 设备丢失。

可能结果：

- 当前代码直接访问 `cp_result->texture_`，如果 `cp_result == nullptr` 会 crash。

风险等级：高。

解决方案：

- 修改为：

```cpp
auto cp_result = frame_carrier_plugin_->CopyTexture(...);
if (!cp_result || !cp_result->texture_) {
    LOGE("CopyTexture failed: empty result or texture");
    return;
}
```

- `CopyTexture()` 返回结构中增加错误码，区分 shared handle 无效、device lost、尺寸变化、内部纹理未创建。
- 连续失败达到阈值时触发采集链重建，而不是每帧继续进入失败路径。
- 对 `target_texture` 后续使用前统一判空。
- 在统计中记录 copy texture fail count，方便线上定位。

## 10. 编码器插件裸指针跨任务/跨线程保存

位置：

- `src/GammaRay/src/render/app/encoder_thread.cpp`
- `encoder_plugins_` 保存 `GrVideoEncoderPlugin*`

触发条件：

- 插件被禁用或释放。
- 编码任务仍在队列中。
- YUV 转换回调再次 `PostEncTask`。

可能结果：

- 访问已释放插件对象。
- 编码器内部已经 `Exit(monitor_name)`，后续回调继续 `Encode()`。

风险等级：高。

解决方案：

- `encoder_plugins_` 不保存裸指针，改保存插件 ID 或 `std::weak_ptr` 包装对象。
- 如果暂时无法改插件所有权，至少在每个异步回调执行前通过 `plugin_manager_->GetPluginById(id)` 重新取一次插件。
- `Exit(monitor_name)` 后清除对应 monitor 的 pending encode task，或者给任务带 generation id，旧 generation 的任务直接丢弃。
- 禁用插件时先停止编码线程队列，再调用插件 `Exit()`。
- YUV 转换回调里不要闭包捕获 `target_encoder_plugin*`，改捕获 `monitor_name` 和 `encoder_generation`。

## 11. `frame_carrier_plugin_` 缺失会导致视频链路不工作，间接诱发空状态

位置：

- `src/GammaRay/src/render/app/encoder_thread.cpp:51`
- `src/GammaRay/src/render/app/encoder_thread.cpp:47`

触发条件：

- `plugin_frame_carrier.dll` 未加载。
- 插件配置缺失或 `OnCreate` 失败。

可能结果：

- 当前 `Encode()` 一开始会 return，不直接 crash。
- 但上游仍认为视频链路存在，可能导致客户端连接后无帧、统计或控制路径进入异常状态。

风险等级：中。

解决方案：

- `frame_carrier_plugin_` 缺失时不要静默 return，渲染端应进入明确的 degraded/failed 状态。
- 启动时检查关键插件清单：frame carrier、至少一个 capture、至少一个 encoder、至少一个 net plugin。
- 缺关键插件时向面板发送 `kRpPluginsInfo` 或专门错误消息，让 UI 明确显示“视频链路不可用”。
- `Encode()` 中首次发现缺失只上报一次，避免刷日志。
- 构建后复制阶段增加插件文件存在性校验。

## 12. D3D 设备生成失败后的资源状态不一致

位置：

- `src/GammaRay/src/render/rd_app.cpp:765`
- `src/GammaRay/src/render/rd_app.cpp:785`
- `src/GammaRay/src/render/rd_app.cpp:802`
- `src/GammaRay/src/render/app/encoder_thread.cpp:185`

触发条件：

- `adapter_uid` 无效。
- 显卡驱动异常。
- RDP/虚拟显示器环境。
- 显示器热插拔。

可能结果：

- 当前部分路径会 return。
- 插件中的 `d3d11_devices_` / `d3d11_devices_context_` 可能保持旧值或空值，后续插件处理帧时 crash。

风险等级：中高。

解决方案：

- `GenerateD3DDevice()` 失败时清理该 `adapter_uid` 的旧 wrapper，避免保留半初始化状态。
- 插件 `d3d11_devices_` 注入前检查 device/context 同时非空。
- 设备生成失败连续出现时触发采集降级：DDA -> GDI -> 软件/停止视频。
- 监听 DXGI device removed/reset，收到后释放编码器、frame carrier、resize plugin 状态并重建。
- 对 RDP/虚拟显示环境做单独路径，不强行按物理 GPU adapter uid 创建。

## 13. Protobuf 解析失败处理不一致

位置：

- `src/GammaRay/src/render/network/ws_panel_client.cpp:156`
- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:382`
- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:498`
- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:598`

触发条件：

- 网络收到损坏包、错协议包、旧版本协议包。
- 恶意或异常客户端发送巨大/非法数据。

可能结果：

- 多数地方有 `ParseFromArray` 检查。
- `WsPanelClient::ParseNetMessage()` 调 `m.ParseFromString(msg)` 没检查返回值，解析失败后继续读取默认字段，通常不 crash，但可能执行错误分支。
- 后续子消息访问可能造成逻辑错误或插件收到不完整数据。

风险等级：中。

解决方案：

- 所有 protobuf 解析统一用布尔返回值检查，`ParseFromString` 失败直接 return。
- 消息处理前检查 `type()` 是否在已知枚举范围内。
- 对外部网络入口增加最大包大小限制，例如 panel/control 消息不超过约定阈值。
- 对子消息按 type 校验 `has_xxx()`，不要只依赖默认字段。
- 错协议包计数，连续异常时断开该 session。

## 14. `PostRendererMessage()` 缺少 `msg` 空指针保护

位置：

- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:487`
- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:490`

触发条件：

- `tc::RpProtoAsData(...)` 或调用方传入空 `Data`。
- 内存分配失败或序列化失败返回空对象。

可能结果：

- `msg->AsString()` 空指针解引用。

风险等级：中。

解决方案：

- `PostRendererMessage()` 第一行补：

```cpp
if (!msg || msg->Empty()) {
    LOGE("PostRendererMessage ignored empty msg");
    return;
}
```

- 如果 `Data` 没有 `Empty()`，至少检查 `msg->Size() > 0`。
- `RpProtoAsData` / `ProtoAsData` 统一约定失败时返回 `nullptr`，调用方都判空。
- `async_send` 前捕获 `std::string payload = msg->AsString()`，避免异步发送期间依赖 `Data` 对象生命周期。

## 15. WebSocket/asio 回调捕获 `this`

位置示例：

- `src/GammaRay/src/render/network/ws_panel_client.cpp:84`
- `src/GammaRay/src/client/network/ct_panel_client.cpp:61`
- `src/GammaRay/src/client/network/ct_spvr_client.cpp:71`
- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:248`

触发条件：

- 网络客户端或服务器对象销毁。
- asio 线程仍在派发 `recv`、`timer`、`connect`、`disconnect` 回调。

可能结果：

- 访问释放后的对象。
- 常见于退出、重连、服务关闭、网络断开。

风险等级：高。

解决方案：

- asio client/server 持有者使用 `shared_from_this` + `weak_ptr` 注册回调。
- `Exit()` 或析构中先 `stop_all_timers()`，再 `stop()`，最后清空回调或置 `exiting_`。
- 回调里不要直接访问成员，先检查 `self && !self->exiting_`。
- `async_send` 回调也要同样保护，不只保护 `bind_recv`。
- 网络对象析构前等待 asio 相关线程停止，避免回调晚于对象释放。

## 16. 文件传输 UI 双击行号越界

位置：

- `src/client_plugins/file_transfer_client/src/widget/file_list_widget.cc:434`

触发条件：

- `QModelIndex` 有效但模型刚刷新，`current_file_container_` 已变化。
- 双击时 row 超出 `files_detail_info_`。
- 搜索/刷新/远端文件列表异步更新与 UI 操作交错。

可能结果：

- `current_file_container_.files_detail_info_[row]` 越界。

风险等级：高。

解决方案：

- 增加工具函数：

```cpp
std::optional<FileDetailInfo> FileListWidget::GetFileInfoByIndex(const QModelIndex& index) const;
```

- 函数内部检查 `index.isValid()`、`row >= 0`、`row < current_file_container_.Size()`。
- `OnRowDoubleClicked()` 只通过这个函数取记录。
- 文件列表刷新前关闭编辑器并清空 `context_menu_index_`。
- 远端文件列表异步更新后，不复用旧 `QModelIndex`。

## 17. 文件传输退出持久编辑器时 row 越界

位置：

- `src/client_plugins/file_transfer_client/src/widget/file_list_widget.cc:565`

触发条件：

- 已打开编辑器后文件列表刷新。
- `context_menu_index_` 指向旧模型行。
- 文件被删除、重命名、搜索过滤。

可能结果：

- 越界访问 `files_detail_info_[row]`。
- Qt 编辑器对象状态和模型状态不一致导致 crash。

风险等级：高。

解决方案：

- `ExitPersistentEditor()` 开头检查 `context_menu_index_.isValid()` 和 row 边界。
- 如果 row 已失效，只关闭编辑状态、清空 editor，不再读取 `files_detail_info_[row]`。
- 模型刷新时先调用 `ExitPersistentEditor(false)`，参数表示“不提交重命名”。
- `QPersistentModelIndex` 可替代 `QModelIndex`，但模型 reset 后仍需检查有效性。
- `item_delegate_->editor_` 建议改为 `QPointer<QLineEdit>`，避免 editor 已销毁后悬空。

## 18. 文件传输“全选”空模型时使用 `rowCount() - 1`

位置：

- `src/client_plugins/file_transfer_client/src/widget/file_list_widget.cc:317`
- `src/client_plugins/file_transfer_client/src/widget/file_list_widget.cc:328`

触发条件：

- 当前目录为空。
- 模型尚未加载完成。

可能结果：

- `index(-1, column)` 通常返回 invalid，不一定 crash。
- 但后续构造 `QItemSelection(topLeft, bottomRight)` 对 invalid index 的行为依赖 Qt 实现，属于风险点。

风险等级：中。

解决方案：

- 全选前检查 `rowCount > 0 && columnCount > 0`。
- 空模型时直接 `selectionModel->clearSelection()` 后 return。
- `topLeft`、`bottomRight` 创建后检查 `isValid()`。
- 两个全选按钮共用一个 `SelectAllRows()` 函数，避免重复修漏。

## 19. 文件传输 `dynamic_cast<RemoteFileUtil*>` 未校验

位置：

- `src/client_plugins/file_transfer_client/src/widget/file_list_widget.cc:348`

触发条件：

- `affiliation_type_ == kRemote` 但 `file_util_` 实际未创建为 `RemoteFileUtil`。
- 构造参数异常或未来扩展引入新类型。

可能结果：

- Qt `connect(nullptr, signal, ...)` 运行时警告或失败，通常不 crash。
- 但如果后续代码假设信号已连接，可能导致状态异常。

风险等级：低中。

解决方案：

- 改成：

```cpp
auto remote_util = dynamic_cast<RemoteFileUtil*>(file_util_.get());
if (!remote_util) {
    LOGE("Remote file util expected but missing");
    return;
}
connect(remote_util, ...);
```

- `Init()` 中创建 `file_util_` 后立即校验非空。
- 对未知 `FileAffiliationType` 明确日志和失败状态。
- 如果后续类型变多，改为在 `BaseFileUtil` 提供统一信号，避免下转型。

## 20. 文件传输持久编辑器已知存在 crash 注释

位置：

- `src/client_plugins/file_transfer_client/src/widget/file_list_widget.cc:386`
- `src/client_plugins/file_transfer_client/src/widget/file_list_widget.cc:397`
- `src/client_plugins/file_transfer_client/src/widget/file_list_widget.cc:561`

证据：

- 注释明确写到不能连续对同一 `QModelIndex` 调 `closePersistentEditor`，否则会崩溃。

触发条件：

- 用户快速点击、重命名、失焦。
- 列表刷新和编辑器关闭交错。

可能结果：

- Qt delegate/editor 生命周期错乱。

风险等级：高。

解决方案：

- 持久编辑器状态集中管理，只允许一个入口打开/关闭。
- 关闭函数做幂等：已关闭、index 失效、editor 为空都直接 return。
- 关闭后不要立即在同一事件栈重新打开编辑器，使用 `QTimer::singleShot(0, ...)` 延后。
- `current_index_opened_` 和 `context_menu_index_` 必须同时更新，不能只改其中一个。
- 增加快速点击/重命名/刷新组合的 UI 回归测试。

## 21. Vulkan/libplacebo 渲染空格式已知会 crash

位置：

- `src/GammaRay/src/client/front_render/vulkan/pl_vulkan.cpp:605`

证据：

- 代码注释明确写到如果 `desc` 为空会导致 libplacebo 崩溃。

触发条件：

- `AVFrame::format` 非法、未初始化或不被 FFmpeg 描述。
- 解码器输出异常帧。

可能结果：

- 当前已经加了 `if (!desc) return false;`，该点已局部防护。
- 同类风险仍存在于后续 `pl_map_avframe_ex`、swapchain、renderer、GPU 资源为空路径。

风险等级：中高。

解决方案：

- 保留当前 `desc` 判空，并继续扩展后续关键对象判空：`frame`、`m_Vulkan`、`gpu`、swapchain、renderer。
- `RenderFrame()` 对每一步失败都先清理已经 map 的资源，再 return。
- 避免 `pl_render_image()` 失败后直接 return 导致没有 unmap；当前这条路径需要补 `pl_unmap_avframe`。
- 解码器输出帧进入 Vulkan 前校验 format、width、height、data/frames context。
- Vulkan 失败超过阈值后降级到 D3D11/OpenGL/软件路径。

## 22. Vulkan 初始化部分成员为空时析构路径可能访问空对象

位置：

- `src/GammaRay/src/client/front_render/vulkan/pl_vulkan.cpp`

触发条件：

- `CreatePlVulkanInstance()` 成功一半后失败。
- `m_PlVkInstance`、`m_Vulkan`、`fn_vkDestroySurfaceKHR`、surfaces 创建状态不一致。

可能结果：

- 析构中销毁 surface 时依赖 `m_PlVkInstance->instance`。
- 当前部分判断存在，但失败中间态仍需运行时验证。

风险等级：中。

解决方案：

- 把 Vulkan 资源改成明确 RAII 小对象，按依赖反向释放。
- 析构中销毁 surface 前检查 `m_PlVkInstance && m_PlVkInstance->instance`。
- 初始化失败时立即调用一个 `Reset()`，清理已经创建的部分资源。
- 每个 map 用 `find()` 检查 key 是否存在，不用 `operator[]` 隐式创建空资源。
- 增加 Vulkan 初始化失败路径测试，包括无 Vulkan、无 surface、无可用 physical device。

## 23. NVENC/AMF 初始化或编码异常

位置：

- `src/render_plugins/nvenc_encoder/nvenc_video_encoder.cpp`
- `src/render_plugins/amf_encoder/video_encoder_vce.cpp`

触发条件：

- 驱动不支持目标编码格式。
- H.265/YUV444/full-color 能力不匹配。
- D3D texture 格式不符合编码器要求。
- GPU device lost。

可能结果：

- 代码里部分 NVENC 异常有 catch。
- AMF/D3D 原生 API 失败后如果下游继续使用空 surface/native pointer，可能 crash。

风险等级：中高。

解决方案：

- NVENC/AMF 插件所有外部 API 调用统一包装为 `Result`，不要让异常或错误码穿透到主编码链。
- 初始化失败时明确释放半初始化 encoder/context/surface。
- 编码失败分级：单帧失败丢帧，连续失败重建 encoder，再失败降级 FFmpeg software。
- full-color/H.265/YUV444 能力选择前先查询插件 capability，不满足时不要尝试初始化。
- D3D texture 进入编码前校验格式、尺寸、adapter uid 和 device 一致性。

## 24. 手写 `memcpy` 对固定数组或外部 buffer 长度校验不足

位置示例：

- `src/GammaRay/src/render/plugins/dda_capture/dda_capture.cpp:569`
- `src/render_plugins/gdi_capture/gdi_capture.cpp:197`
- `src/GammaRay/src/render/app/win/app_manager_win.cpp:89`
- `src/render_plugins/frame_carrier/video_frame_carrier.cpp:335`

触发条件：

- 显示器名称、路径、纹理尺寸、图像 buffer 大小超出目标容量。
- 源 buffer 实际大小小于计算大小。

可能结果：

- 栈/堆内存破坏。
- 延迟 crash，定位困难。

风险等级：高。

解决方案：

- 固定数组复制统一用安全 helper，例如：

```cpp
template <size_t N>
void CopyCStringToArray(char (&dst)[N], std::string_view src) {
    const auto n = std::min(src.size(), N - 1);
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}
```

- 图像/纹理 buffer 复制前检查目标容量和源长度。
- 对 `DataAddr()` 返回值判空，对 `Size()` 做上限检查。
- 对显示器名称、路径、进程参数这些外部长度字段统一截断并打日志。
- 开启 ASan/UBSan 或 Windows PageHeap 做专项验证。

## 25. RTC DataChannel TLV 包长度不可信

位置：

- `src/render_plugins/net_rtc/rtc_data_channel.cpp:60`
- `src/render_plugins/net_rtc_local/rtc_data_channel.cpp:60`

触发条件：

- 收到损坏或恶意 data channel 包。
- TLV header 中的 `this_buffer_length_` 大于实际 payload。

可能结果：

- `memcpy` 越界读取。
- 后续组包状态损坏。

风险等级：高。

解决方案：

- 读取 TLV header 前先检查 `buffer.size() >= sizeof(NetTlvHeader)`。
- 检查 `header->this_buffer_length_ <= buffer.size() - sizeof(NetTlvHeader)`。
- 检查分片字段：总长度、起始偏移、当前长度不能溢出，且不能超过最大消息大小。
- 对异常包直接丢弃并增加错误计数，超过阈值关闭 data channel。
- TLV header 建议加 magic/version/checksum，避免误把随机数据当协议包。

## 26. 剪贴板虚拟文件 COM 对象生命周期复杂

位置：

- `src/render_plugins/clipboard/win/cp_virtual_file.cpp:65`
- `src/render_plugins/clipboard/win/cp_virtual_file.cpp:125`
- `src/client_plugins/clipboard/win/cp_virtual_file.cpp:60`
- `src/client_plugins/clipboard/win/cp_virtual_file.cpp:117`

触发条件：

- Windows 剪贴板请求异步读取虚拟文件。
- `IStream` 持有的对象被插件释放。
- `GlobalLock` 后数据结构不符合预期。

可能结果：

- COM 引用计数错误。
- use-after-free。
- 剪贴板文件拖拽/粘贴时 crash。

风险等级：中高。

解决方案：

- COM 对象使用严格引用计数审计，`QueryInterface/AddRef/Release` 必须成对正确。
- `pmedium->pstm` 返回前确保 stream 对象生命周期独立于插件短期栈对象。
- `GlobalLock` 后校验 `GlobalSize` 足够容纳 `FILEGROUPDESCRIPTOR` 和文件描述数组。
- 异步剪贴板传输期间禁止插件销毁，或让传输任务持有共享生命周期 token。
- 增加大文件、多文件、拖拽取消、远端断开时的剪贴板回归测试。

## 27. Windows message window 的 `GWLP_USERDATA` 裸指针风险

位置示例：

- `src/client_plugins/clipboard/win/win_message_window.cpp:35`
- `src/GammaRay/src/render/app/win/win_render_message_window.cpp:36`
- `src/GammaRay/src/render_panel/system/win/win_panel_message_window.cpp:34`

触发条件：

- 窗口销毁后仍收到消息。
- `GWLP_USERDATA` 已清零或指向已释放对象。

可能结果：

- 窗口过程访问空/悬空 `self`。

风险等级：中高。

解决方案：

- 窗口过程取 `self` 后先判空，空则调用 `DefWindowProc`。
- `WM_NCDESTROY` 中清空 `GWLP_USERDATA`，并设置对象内部 `hwnd_ = nullptr`。
- 对象析构时先销毁窗口，再停止消息循环线程。
- 避免 `PostQuitMessage(0)` 影响非本窗口消息循环，确认它只在专用线程使用。
- 窗口消息回调中不要访问可能已释放的插件对象，改走 weak token。

## 28. `shared_from_this()` 调用依赖对象已由 `shared_ptr` 托管

位置示例：

- `src/GammaRay/src/render/rd_app.cpp:100`
- `src/GammaRay/src/render_panel/gr_application.cpp:74`
- `src/GammaRay/src/client/ct_base_workspace.cpp:104`

触发条件：

- 未来有人把这些类改成栈对象或裸 `new` 管理。
- 在构造函数中提前调用 `shared_from_this()`。

可能结果：

- 抛 `std::bad_weak_ptr`，未捕获时进程终止。

当前状态：

- 当前主路径看起来是通过 `std::make_shared` 创建后再调用，暂时可控。

风险等级：低中。

解决方案：

- 保持当前创建方式：所有继承 `enable_shared_from_this` 的类只用 `Make()` 或 `std::make_shared` 创建。
- 把构造函数设为 private/protected，强制外部走 `Make()`。
- 禁止在构造函数中调用 `shared_from_this()`，只允许在 `Init()` 或之后调用。
- 对关键 `shared_from_this()` 可加注释说明前置条件。
- 如要更稳，`Make()` 内创建对象后立即调用 `Init()`，避免调用方顺序错误。

## 29. 人工 crash 函数仍留在代码中

位置：

- `src/GammaRay/src/render_panel/ui/tab_base.cpp:14`
- `src/GammaRay/src/render_panel/ui/tab_base.cpp:30`

触发条件：

- `CrashFunction()` 当前调用被注释。
- 如果调试时恢复，会立即空指针写入。

可能结果：

- 必现 crash。

风险等级：低，但应删除或用明确的 debug 宏包住。

解决方案：

- 直接删除 `CrashFunction()`。
- 如果确实需要测试 Breakpad，改成：

```cpp
#ifdef ENABLE_MANUAL_CRASH_TEST
void CrashFunction();
#endif
```

- 手动 crash 入口不要挂在语言切换等普通业务路径上。
- 用命令行参数或隐藏 debug 菜单触发，并且只在 Debug/Internal 构建启用。

## 30. WebClient 浏览器端解码/渲染异常

位置：

- `src/GammaRayWebClient/src/renderer/gr_renderer_manager.ts`
- `src/GammaRayWebClient/src/client/gr_ws_conn.ts`
- `src/GammaRayWebClient/src/client/gr_rtc_direct_conn.ts`

触发条件：

- wasm 解码器加载失败。
- 收到 H.265 但代码仍用 H.264 decoder id。
- WebGL 初始化失败。
- WebSocket 收到非 protobuf 二进制。
- RTC SDP 或 track 异常。

可能结果：

- 浏览器页面异常、渲染中断、Promise rejection。
- 不会让 native 进程 crash，但用户看到黑屏或页面崩溃。

风险等级：中。

解决方案：

- WebSocket 收包解析包裹 `try/catch`，protobuf decode 失败时丢弃该包。
- 根据 `msg.videoFrame.type` 选择 H.264/H.265 decoder，不要固定 H.264。
- wasm 加载失败、decoder open 失败、renderer init 失败时显示错误状态并停止继续 decode。
- `onVideoFrame` 中校验 key frame、data length、frameWidth/frameHeight。
- RTC direct 中移除硬编码 device/stream，URL 参数缺失时直接报错，不发请求。

## 31. 本地数据库/SharedPreference 初始化失败后的后续访问

位置：

- `src/GammaRay/main.cpp`
- `src/GammaRay/src/render/rd_app.cpp`
- `src/GammaRay/src/render_panel/gr_context.*`

触发条件：

- ProgramData 无权限。
- 数据文件被占用。
- SQLite 文件损坏。

可能结果：

- 面板入口对 `SharedPreference::Init` 有失败返回。
- 其他子模块如果假设数据库 operator 一定存在，可能空指针。

风险等级：中。

解决方案：

- `SharedPreference::Init`、数据库初始化失败后明确进入只读/失败状态，不继续构造依赖数据库的 manager。
- `GrContext::Init()` 中每个 operator 创建后判空。
- 数据库访问入口返回 `Result<T, Error>`，不要让调用方默认 operator 一定存在。
- SQLite 打开失败时尝试备份损坏文件并重建新库。
- ProgramData 目录创建失败时弹出明确错误，不继续启动完整服务。

## 32. HTTP 静态文件路径拼接风险

位置：

- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:214`
- `src/render_plugins/ssl_proxy/ssl_proxy_server.cpp:95`

触发条件：

- 请求路径经 `url_decode` 后包含异常路径。
- `fill_file` 对不存在或越界路径处理不完整。

可能结果：

- 常见是 404 或返回错误。
- 如果底层库对路径/文件句柄处理不稳，可能异常。

风险等级：低中。

解决方案：

- 静态文件服务只允许访问固定根目录，先 canonicalize，再检查结果路径仍在根目录下。
- 拒绝包含 `..`、绝对路径、驱动器前缀的请求。
- `fill_file` 前检查文件存在、是普通文件、大小在允许范围内。
- 对 URL decode 失败或非法 UTF-8 路径直接返回 400。
- ssl proxy 和 panel server 共用同一个安全路径解析函数。

## 33. 面板全局 `grApp` 使用时机

位置：

- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:474`
- `src/GammaRay/src/render_panel/network/ws_panel_server.cpp:661`
- `src/GammaRay/src/render_panel/gr_application.cpp`

触发条件：

- `WsPanelServer` 在 `grApp` 设置前触发同步。
- 应用退出时 `grApp` 已释放或部分对象为空。

可能结果：

- 空指针或悬空全局指针访问。

风险等级：中。

解决方案：

- 避免在 `WsPanelServer` 内使用全局 `grApp`，优先使用构造时传入的 `app_`。
- `RpSyncPanelInfo()` 中所有 `grApp->...` 改为 `app_->...` 并判空。
- 全局 `grApp` 退出时显式 reset，访问前检查。
- 初始化顺序调整：先设置全局/成员应用对象，再启动可能触发回调的 server/timer。
- 对 companion/auth 缺失维持默认值，但要日志记录。

## 34. UI 定时器 lambda 生命周期

位置示例：

- `src/GammaRay/src/render_panel/gr_workspace.cpp:460`
- `src/GammaRay/src/render_panel/gr_workspace.cpp:521`
- `src/GammaRay/src/client/ct_base_workspace.cpp:74`
- `src/GammaRay/src/client/ct_base_workspace.cpp:88`

触发条件：

- 窗口关闭后 `singleShot` 回调执行。

可能结果：

- 有些位置用 `QPointer` 防护。
- 也有位置直接捕获 `this`，存在 use-after-free 风险。

风险等级：中高。

解决方案：

- 所有 `QTimer::singleShot` 指定 QObject context：`QTimer::singleShot(ms, this, [this]{ ... })`，让 Qt 在对象销毁后自动取消。
- 对没有 context 的 lambda 改用 `QPointer`。
- 回调执行前检查窗口是否 still visible/initialized。
- `closeEvent`/析构中停止自有 timer。
- UI 层不要把后台任务结果直接投递到裸 `this`，用 `QPointer` 包装目标 widget。

## 35. 线程退出顺序与 Qt 事件循环退出顺序不一致

位置：

- `src/GammaRay/src/render/rd_app.cpp:953`
- `src/GammaRay/src/render/rd_app.cpp:956`
- `src/GammaRay/src/render/rd_app.cpp:962`

触发条件：

- `QApplication::exec()` 退出。
- 音频采集线程、编码线程、插件线程、asio 线程仍在投递任务。

可能结果：

- 退出阶段访问已销毁 QObject。
- 插件已 `OnDestroy`，但线程仍调插件。

风险等级：高。

解决方案：

- 统一退出顺序：停止入口流量 -> 停止 timers -> 停止网络 -> 停止采集 -> drain 编码队列 -> 停插件 -> 释放 Qt 对象。
- `Exit()` 设置 `exiting_ = true` 后，所有新任务投递直接拒绝。
- 线程类增加 `DrainAndStop()`，先不接新任务，再执行或丢弃队列，再 join。
- 插件 `OnStop()` 必须同步等待内部线程退出。
- Qt 事件循环退出前完成核心资源停止，不要等析构阶段再停线程。

## 建议优先处理顺序

1. 给插件管理器和网络/asio 回调引入生命周期令牌或 `weak_ptr` 防护。
2. 修复 `capture_plugin_` 切换后直接调用的问题，所有 `StartCapturing()` 前重新判空并检查切换返回值。
3. 修复 `encoder_thread.cpp` 中 `CopyTexture()` 返回空对象的空指针风险。
4. 文件传输 UI 所有 `files_detail_info_[row]` 前做 row 边界检查，持久编辑器关闭前确认 `QModelIndex` 仍属于当前模型。
5. 所有 `memcpy` 到固定数组的位置统一改为带容量检查的复制。
6. RTC data channel 解析先校验 header 长度和 payload 长度。
7. `PostRendererMessage()`、插件事件回调、`On1Second()` 这类基础设施补空指针和退出态保护。
8. 把 `CrashFunction()` 删除或放到 `#ifdef DEBUG_CRASH_TEST` 下。
