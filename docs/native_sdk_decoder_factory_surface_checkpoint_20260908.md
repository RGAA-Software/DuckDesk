# SDK 解码工厂与 Android 输出生命周期（2026-09-08）

## 平台解码入口

- `ThunderSdk::Init` 接收客户端注入的 `VideoDecoderFactory`，不再接收 `void* Surface` 或 `DecoderRenderType`。
  Windows/Android 的组合入口各自创建真实平台工厂，未提供默认空后端或旧接口兼容入口。
- 共享 SDK 不再包含具体解码器头文件或平台构造分支；解码器初始化与失败后的软件回退归工厂。
  Windows 保留 D3D11/FFmpeg 或 Vulkan 首选、旧软件解码器回退；Android 保留 MediaCodec 首选、Android 软件解码回退。
  Android 工厂限制单路显示解码，Windows 仍可多显示器。未增加手机自身屏幕。
- 初始化失败先释放部分资源，再创建回退后端。Android 的 MediaCodec 初始化失败继续记住该显示器禁用硬解状态。
- Windows 的 Vulkan 上下文在创建视图后才补齐，工厂仍在实际创建解码器时读取，不在 SDK 初始化时过早复制空句柄。
  `ThunderSdkParams` 的硬件字段仍未迁出，此处不是硬件资源隔离全部完成。
- 旧 `sdk_decoder_render_type.h` 完整归档后退出活动源码；共享解码器 Init 不再接受 Surface。
  FFmpeg 外部 C API、已有设备上下文和编码范围保持不变。

## Android Surface 所有权与切换

JNI 取得的 `ANativeWindow` 继续由带 `ANativeWindow_release` 删除器的智能指针拥有。
平台层的 `AndroidVideoOutput` 保存线程安全的窗口快照；MediaCodec 与软件解码器各自保留正在使用的快照。
共享 SDK、解码基类和 NativeSession 的交接链不再用 `void*` 或整数地址传递窗口。

`RefreshVideoOutput` 只接收输出可用状态、完成回调及平台配置动作；动作捕获智能指针，在解码线程上先执行配置，再刷新解码器。
MediaCodec 更新成功才释放旧窗口引用；更新失败则释放旧解码器，等待完整关键帧重新创建。旧窗口至少保持到该轮完成回调。
窗口状态使用短临界区互斥锁；不依赖当前 NDK 尚未支持的 `atomic<shared_ptr>` 特化，旧引用在锁外释放。

首轮真机暴露原有 detach 空操作的问题：进入文件页后 Surface 已销毁，解码器仍向旧窗口输出。
返回后 `setOutputSurface` 报 Released state，随后通过解码器重建恢复到了 59 帧。这不是“无错误直接切换成功”。
本轮据此把 detach 纳入输出队列：输出不可用时丢弃显示解码任务并释放解码器，恢复后绑定新窗口、请求关键帧。
网络、音频和录制前的编码数据回调不因隐藏显示视图而停止；完整长时间后台/锁屏验收仍不在本轮范围。

## 生命周期修正

SDK 拒绝缺失工厂或参数、重复 Init，Start 在初始化失败和退出后不启动工作；重复 Start 不再创建第二组工作线程。
退出顺序调整为：停止网络/计时入口 → 停止工作线程 → 释放并清空解码器 → 释放工厂。
此前先访问解码器 map 再停止视频线程，可能与创建/解码并发；同时 map 未清空会保留解码器对 SDK 的引用。

新增测试覆盖工厂注入/释放、部分初始化失败、重复启动/停止、输出完成回调内退出、排队对象释放、
detach 后不执行显示任务、reattach 后恢复、配置动作先于完成回调，以及退出后不执行配置动作。
SDK 源码边界检查同步禁止共享会话重新引入具体后端和旧 Surface 接口。

## 验证与交付

- Windows 使用 `build_cpp_tests.bat px_client test_sdk_decoder_factory test_av_frame_ownership` 增量构建成功；
  Android `:core-native:testDebugUnitTest :app:assembleDebug` 成功（48 秒），未运行发布全量构建。
- 最终 8 组 CTest 全部通过（27.20 秒）：源码分层、解码工厂（6 个用例）、连接参数、UDP 状态、帧所有权、
  UDP 失败处理、码流辅助及 WebSocket 重连。工厂生命周期测试验证 SDK 部分初始化失败；未模拟真实硬件初始化故障。
- C++ 所有权/150 列检查、Native SDK、Android、Windows 通道约束、WebRTC DLL 边界检查及 `git diff --check` 通过。
  新工厂、输出对象及新增测试的 `clang-format --dry-run --Werror` 通过；包含旧文件全文的格式检查仍报告历史格式差异，
  诊断保存在 `sdk-decoder-factory-format-legacy.log`，未为此机械重排无关旧代码，不能视为全仓格式检查通过。
- Windows 发布目录 20 秒实连通过：窗口创建、UDP 就绪、实际解码均成功，无解码错误。
  Client、APM DLL 与语言资源已同步到 `build_official/dist` 并逐项核对 SHA-256，`px_service` 已恢复 Running。
- Android 使用 `adb install -r -d` 覆盖安装，未卸载或清理数据。最终真机短测为 00:53:27–00:54:07（约 40 秒）：
  初始 MediaCodec 硬解 60 帧/秒；打开文件传输页再返回，重新创建解码器后恢复 59 帧/秒、丢包 0.0%。
  本次进程日志未再出现 `MediaCodec output surface update failed`、`Released state` 或崩溃标记，结束会话正常退出 SDK。
  此结果验证的是 detach/rebuild/reattach 路径，不是所有设备的原地 `setOutputSurface` 切换保证。

最终 Client 构建树与 dist 的 SHA-256：
`BA8D425ED881C354219683948C124902FF88442A5A49055B379863A031C9C90F`。

最终 Debug APK 与手机已安装 base.apk 的 SHA-256：
`2E9DE0776EAB2FEE2BFEB3C420FEED7E4AF258E01B24C637262BA77B6ED1902A`。

日志位于本机 `test-results/sdk-decoder-factory-*.log`，不提交运行时凭据或设备截图。
旧实现保存在 `backup/native_sdk_decoder_factory_surface_20260908`，22 份源文件的原始字节与 SHA-256 清单已核验。

## 剩余边界

硬件上下文从公共参数迁出、CPU/GPU 帧统一所有权、旧字节指针解码接口和独立 SDK 配置/集成入口仍未完成。
iOS/macOS 尚未适配；未增加公网 P2P/Relay、统一能力协商或 Native WebRTC。
本轮不代替人工听音、完整锁屏/网络切换、浏览器 WebRTC 或 Release 验收。
