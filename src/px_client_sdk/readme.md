# Pixels Native SDK 源码接入

支持 Windows、Android；iOS/macOS 的真实平台适配尚未实现，配置时明确拒绝。
Native 使用 UDP/FEC 视频、UDP 系统音频及 WS/WSS 控制/文件，不提供协议选择或媒体回退。
语音实时帧目前仍走 WS，UDP 迁移属于后续语音收尾，不能把构建完成视为该项已完成。

## 最小接入

通过源码 checkout 和 CMake target 消费，不需要设置 `PX_PROJECT_PATH`、SDK 内部 include 或 protobuf build-tree 路径：

```cmake
cmake_minimum_required(VERSION 3.26)
project(MyPixelsHost LANGUAGES C CXX)
# 工具链必须在 project() 之前通过命令行或 preset 设置。
add_subdirectory("D:/GoCloud/GammaRayPremium/src/px_client_sdk" pixels_sdk)
add_executable(my_host main.cpp)
target_link_libraries(my_host PRIVATE pixels::sdk)
```

Windows 使用项目已有 vcpkg `x64-windows-static-release` 和一致的静态 CRT，例如配置参数：

```text
-DCMAKE_TOOLCHAIN_FILE=C:/source/vcpkg/scripts/buildsystems/vcpkg.cmake
-DVCPKG_TARGET_TRIPLET=x64-windows-static-release
-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
-DCMAKE_BUILD_TYPE=RelWithDebInfo
```

Android 使用 NDK 29、API 31+、arm64-v8a、`c++_shared` 和现有 `arm64-android` vcpkg 依赖。
以 vcpkg 工具链为入口，`VCPKG_CHAINLOAD_TOOLCHAIN_FILE` 指向 NDK 的 `android.toolchain.cmake`。
其它 ABI/NDK 组合未验收，不宣称已支持。

依赖仍位于同一 checkout 的 `src/px_deps`，FFmpeg/Opus/Protobuf/fmt/spdlog/miniz 来自现有工具链。
Android SSL 复用仓库已有静态库。不要将 SDK 目录单独复制后声称是零依赖发行包。
首版支持源码集成，不承诺预编译包、安装式 find_package 或稳定二进制 ABI。

## 目标与头文件

| 接入目标 | 用途 |
|---|---|
| `pixels::sdk` | 会话、媒体任务与所选平台解码工厂，应用通常只链接此目标 |
| `pixels::transport` | 已认证连接、协议及网络状态；不构建解码器、FFmpeg 或 Opus |

仅网络核心配置 `-DPX_SDK_CORE_ONLY=ON`，只链接 `pixels::transport`。
`px_sdk_core` / `px_sdk_platform` 是内部 object target；完整 SDK 的最终 archive 包含其实现，不要求消费者拼装 object 文件。
Windows Client 和 Android JNI 都链接 `pixels::sdk`，没有第二份消费者专用 SDK 实现。

接入头：`thunder_sdk.h`、`sdk_params.h`、`sdk_messages.h`、`sdk_video_decoder_factory.h`、`gl/raw_image.h`；
低层网络接入使用 `sdk_net_client.h`、`sdk_connection_params.h`。
可使用 `<px_client_sdk/thunder_sdk.h>` 或 `<thunder_sdk.h>` 两种形式。
具体 decoder、connection、FFmpeg 转换 helper 和 CMake source-set 文件属于实现细节，不作为宿主扩展 API。
公开头的传递依赖由 CMake 目标传播，不承诺内部头的源码稳定性。

平台工厂及资源头分别在 `platform/windows`、`platform/android`。
Windows 宿主创建 `WindowsVideoResources`，Android 宿主创建持有 ANativeWindow owner 的 `AndroidVideoOutput`，
随后注入真实平台工厂。不提供占位 Apple 后端或 SDK 内置窗口。

## 生命周期与线程

1. 宿主创建 `MessageNotifier`、会话配置和平台资源；宿主提供设备名及 `client_type_`，公共层不查询 Windows 主机名。
2. `ThunderSdk::Make(notifier)` 后调用 `Init(params, factory)`，检查结果；Init 前必须完成 Windows 资源发布。
   传入后不再并发改写配置/资源；网络持有单次尝试的配置快照。
3. 在 Start 前安装回调。`Start()` 启动工作线程及网络，真实连接需要宿主完成授权，不能在示例中嵌入票据。
4. 编码帧回调来自网络接收路径，视频显示来自视频任务线程，解码音频来自音频任务线程；这些都不是 UI 线程。
   回调只做有界工作，以 weak owner 投递到宿主 UI/功能队列，排队数据必须捕获 owning buffer/frame。
5. 输出替换使用 `RefreshVideoOutput` 的视频线程交接；Android detach 不能直接释放解码器正在使用的窗口。
   GPU 帧必须持有到最后一个消费者结束。不要保留 `span` 代替其 owner。
6. 宿主序列化 Init/Start/输出变更/Exit 操作。退出时先停止功能订阅及新 UI 请求，再 `Exit()`、释放帧和平台资源，
   最后停止宿主拥有的 notifier。回调内不能同步等待同一队列完成宿主任务。
7. 重复 Exit 安全；已停止会话不复活。用户重新启动时创建新 SDK，授权/续票由宿主提供，不复用已消费的一次性票据。
   运行中的 socket 自动重连只推进连接 generation，不销毁整个 SDK 或重建已有文件任务。

文件复用 `FtAsyncSession`，录制复用 `RecordingSession` / `RecordWriter`，语音复用现有共享音频模块；其余公共编排仍按 P3 收尾。
本节不是宣称所有业务均已搬入 ThunderSdk，也不承诺所有历史统计对象均为每会话独立实例。

### 编码录制

`sdk_recording_session.h` 属于完整 `pixels::sdk`，依赖共享 `px_media_record` / FFmpeg 封装库，不进入 `pixels::transport`。
每次录制用 `RecordingSession::Create` 创建独立 run，检查 `Start()`，再从 SDK 的编码视频和编码音频回调 `Submit`。
不要从通用 raw 消息回调录音：UDP 音频不经过该入口。提交后不能修改同一个 Message；单次 run 拒收 Stop 后的迟到数据。

模块只接收 H264/HEVC Annex-B 和 48 kHz 双声道 Opus；丢包空帧不写入 MP4，不将播放 PLC 当原始 Opus。
队列默认限制 32 MiB / 256 包；溢出会失败并收尾，不悄悄丢弃编码视频。等待首个完整关键帧后才创建可封装分段。
`Stop()` 非阻塞拒收并排空；`Completion()` 在 writer 收尾和 finished 回调结束后就绪。
析构等待 worker，worker 内析构会延迟 join；回调不得同步等待同一 worker，不得依赖宿主停止时持有的锁。

结果仅报告实际成功封装的目录；打开、写入、trailer、关闭失败会保留错误。失败分段保留 `.recording` 标记。
每轮文件名有独立标识，重叠收尾不会覆盖下一轮。Windows 保存路径、Android staging/MediaStore 发布仍由宿主负责。
UDP 音频丢包标记按顺序保留采样时长空隙，不伪造 Opus 数据；该行为有确定性测试。
未引入统一采集时间戳协议，仍使用现有视频接收时间和音频采样时钟；长时间损网同步不在本批验收范围。

### 剪贴板文件协议

`sdk_clipboard_protocol.h` 通过 `pixels::transport` 可用，不依赖 Qt/JNI、解码器或文件选择器。
双端以 `ClipboardReadRequest` 校验文件标识、序号、偏移、请求长度和实际载荷；`ClipboardPendingRead` 在发送前预留身份，
取消/发送失败后不复用该序号。调用方序列化 tracker 的访问，不能将同步请求的引用保留到异步回调。
每块最多 128 KiB，为可靠通道的 protobuf/TLV 头留空间；只允许读取已发布大小范围内的字节。

本机文件发布使用每次独立的 opaque 标识，宿主在自己的授权表中解析实际路径，不能打开对端直接传入的路径。
替换剪贴板或停止模块会撤销旧标识。Windows OLE 与 Android 系统剪贴板/缓存/文件授权仍由宿主实现。
这些规则覆盖两个 Native 客户端；不代表 Render 用户代理、Panel 或所有旧平台边界已完成同等重构。

### 语音协议

`sdk_voice_protocol.h` 位于 `pixels::transport`，统一请求/响应与语音配置/帧的构造。
`VoiceCallRequestSequence` 由控制器拥有，callId 与 requestId 一起匹配；不能把仅构造了一条语音帧理解为已经走 UDP。
`sdk_voice_call.h` 位于完整 `pixels::sdk`，`VoiceCallController::Create` 接收不可变的 device/stream 路由与明确依赖。
同一控制器可顺序开始多次通话，但每次使用独立 run、callId、请求序号、超时、包队列和音频端口。
收到匹配的同意后才创建/启动音频，挂断与迟到响应、设备错误、旧媒体包不能复活下一轮。

宿主注入独立的 `send_control` / `send_audio`、音频端口工厂、串行异步任务队列及状态回调。
发送返回值表示本地接纳，不代表对端收到。Windows/Android 的音频入口调用 `PostVoiceAudioMessage` 直接走已关联 UDP，
控制请求/响应/配置/挂断仍走 WS；收到 WS 语音帧直接丢弃，无传输选择或媒体回退。
`post_task` 必须投递到串行队列，不可在音频设备回调栈内直接执行关闭任务；状态回调按 revision 丢弃过期结果。
UI 只映射 `VoiceCallStatus`；Android JNI 显式转换为既有 0/1/2 UI 状态，不直接转换共享枚举的整数值。

`platform/voice_audio_endpoint_port.h` 是宿主可选适配器，包装现有 `VoiceAudioEndpoint`；使用它需显式链接 `px_voice_call`。
独立 SDK 只链接不含设备的 `px_voice_call_core`，不引入 SDL、AAudio、WASAPI 或 APM DLL。
`VoiceAudioPort` 每通话实例独占设备会话，Start 一次；Stop 须覆盖部分初始化与并发取消，并可重复调用。
宿主设备选择、Android 录音权限/路由仍留平台；浏览器 WebRTC 处理不经过 Native 控制器。

调用 `Stop` 结束当前通话，之后可重新 Start；会话退出调用终结性的 `Close`，先于网络和宿主任务队列关闭。
SDK/网络关闭前停止宿主命令入口。设备错误通过弱引用排队清理，队列拒收时将清理转移到独立工作线程。
回调不得持有关闭流程需要的宿主锁，不得等待自身队列；声音质量仍需真实设备与人工听音验收。
宿主收到 `SdkMsgUdpMediaUnavailable` 时调用 `SetTransportAvailable(false)`，结束语音并阻止媒体故障期间重新呼叫，
不修改设备在线状态或文件连接。当前 UDP 故障是该会话的终结性媒体状态，恢复由用户启动新会话完成。

UDP Voice 使用独立包类型 4，不复用系统声音包。每包有当前 association、callId、序号、64 位采集时间和 Opus，
最大 1400 字节、不分片；Opus 1–1275 字节，两个身份字段各 1–128 字节，总长仍须满足单包预算。
发送队列有独立的 8 包容量，取消回调也会释放容量；不等待文件可靠队列。接收保持原通话防重放与 Opus jitter/PLC 机制。
Render 从当前已绑定端点获得 logical-session/stream 身份，并在交付前重新验证绑定；旧关联/旧通话不能复活。
沿用现有关联校验，不宣称逐包加密认证；Native/Render 需同版本部署。WebRTC 的信令和音轨保留原路径。

## 独立消费验证

仓库根目录运行，均不配置/启动 Windows UI、Gradle、npm 或 Rust workspace：

```bat
build_cpp_sdk_standalone.bat windows full
build_cpp_sdk_standalone.bat windows core
set ANDROID_NDK_HOME=D:/android/sdk/ndk/29.0.14206865
build_cpp_sdk_standalone.bat android full
build_cpp_sdk_standalone.bat android core
```

可通过 `VCPKG_ROOT`、`CPP_BUILD_JOBS` 指定依赖位置和并行度；输出分别在 `build_sdk_<platform>_<mode>`。
`CPP_SDK_TARGETS` 可指定额外测试 target，默认只构建 `pixels_sdk_lifecycle` 及其真实依赖。
Windows 自动运行 20 秒超时的离线生命周期测试；Android 只交叉编译，需 adb 执行产物才算运行验证。

`examples/lifecycle` 是独立消费工程：循环创建/初始化并重复关闭真实对象，未 Start 网络，不要求服务器或 UI。
完整模式注入真实解码工厂，不使用假解码器。它证明构建、链接和生命周期接入，不代替首帧、听音或文件功能验收。
独立工程主动拒绝 Qt/RTC/Relay/宿主工具目标；core 模式额外拒绝 codec/platform target。

`BUILD_TESTING=ON` 可配置 SDK 行为测试，不要求 Qt。文件 UI harness 是单独的
`PX_SDK_BUILD_QT_TESTS=ON` 选项，仅用于已提供 Qt/文件引擎的宿主工程，不能作为 SDK 基础依赖。
