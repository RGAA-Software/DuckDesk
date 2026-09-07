# Native SDK Vulkan 显示帧所有权检查点

日期：2026-09-07。基础提交：`0b7726d42`。

## 实施结果

此前 `RawImage` 保存解码器的借用 AVFrame；即使 UI 排队任务持有 RawImage，解码器下一次收帧、复用或释放仍会改变其内容。
现在通过 `av_frame_clone` 创建独立描述符，以带 FFmpeg deleter 的智能指针持有，底层缓冲区及硬件帧上下文按 FFmpeg 引用计数保留。
这不是硬件图像的深拷贝，也不意味着 Vulkan 设备本身的平台生命周期已经解耦。

- RawImage 的 AVFrame 成员为 `shared_ptr<const AVFrame>`；克隆保留帧资源、宽高和颜色标记。
- Vulkan 显示路径同时接收硬件帧和软件解码的 CPU 帧，后者由 libplacebo 上传；不增加任何传输兼容路径。
- SDK 工作帧与 Windows 格式探测帧使用 RAII。探测用 AVPacket 的错误分支也自动释放。
- 解码器析构及初始化失败调用清理；重复清理不会对空上下文收帧，任意收帧错误均终止清理循环。
- Decode 在同一互斥锁内检查帧与上下文，避免检查后被 Release 释放。
- libplacebo 映射由同步作用域守卫解除，覆盖开始交换链、渲染及提交失败分支。
  按仓库 libplacebo `swapchain.h` 的契约，每次成功 start 均配对 submit，即使 render 失败也不能直接返回。

原实现的 11 个文件完整保存在 `backup/native_vulkan_frame_ownership_20260907`；manifest 的 SHA-256 已逐项验证。
第三方源码、Web/Render RTC 和工作区已有 Rust 改动未修改。

## 验证与交付

Windows 使用聚焦增量构建，未运行 `build_official.bat`：

```text
build_cpp_tests.bat px_client test_av_frame_ownership test_udp_media_failure test_sdk_websocket_reconnect
```

Android `gradlew.bat :app:assembleDebug` 成功。无手机，未安装、卸载或清空数据。

最终 7 组 CTest 全部通过，共 15.23 秒：

- common_message_notifier、common_async_runtime
- test_udp_media_state、udp_media_failure
- sdk_stream_helper、sdk_websocket_reconnect
- av_frame_ownership：新增 6 个用例，验证源帧写时复制/销毁、克隆元数据、排队消费、取消释放、64 次引用生命周期及无效输入。

帧单测使用真实 CPU FFmpeg 缓冲区；用于模拟硬件载体的描述符不送入 GPU。
它们不证明硬件解码、GPU 丢失、驱动释放或 GPU 异常注入已经验收。
既有回调测试继续覆盖分发中注销、回调内关闭和重复启停。

首次 20 秒冒烟暴露了新增像素格式校验过严的问题：Vulkan 显示也使用软件解码帧，不能只接受 `AV_PIX_FMT_VULKAN`。
修正校验并补上软件帧用例后，双端重新编译及上述测试通过。
发布重启服务后曾因 Render 尚未启动而跳过一次冒烟启动；确认其恢复后重试。
最终 20 秒本机默认 UDP 直连检查通过：进程和窗口存在、UDP 媒体就绪、持续解码，无解码错误。
这不是硬件解码矩阵或浏览器功能验收；累计测试运行时间低于 5 分钟。

Client、语音 APM DLL 和语言资源已同步至 `build_official/dist`，构建树与 dist 的 SHA-256 一致。
发布期间停止的服务和 Render 已恢复，Panel 保持运行，冒烟创建的 Client 已退出。

| 产物 | SHA-256 |
|---|---|
| Client（构建树与 dist 相同） | `40FC28429CF94FDA7AA8A38B8AA7E11FF6AD14957AB5BCB8699151515E9AFD79` |
| Android Debug APK | `A21A3642E5602351609EB2413EB1F3772C2A264BEFC51021B24263D3B8DA13CE` |

SDK 路径/单一传输、Windows 入口、Android JNI 与传输、WebRTC 链接边界和 C++ ownership 检查通过。
新增文件及修改的渲染函数按仓库 clang-format 格式处理，没有整文件重排无关旧实现。
本机验证日志为 `test-results/vulkan-frame-*`，其中 `windows-smoke-final.log` 为最终成功记录。

## 后续范围

1. CPU RawImage 缓冲区、D3D11 纹理、Vulkan 设备 AVBufferRef 和 Android Surface 的所有权及接口。
2. SDK 内其余旧解码器上下文/packet/C ABI 边界，以及实际 Windows/Android 平台适配目标和独立构建。
3. 核对原生语音实时传输；本轮未改变音频、APM 和文件通道。
4. iOS/macOS 适配与 Android 真机功能仍未实施；Release 所需 FFmpeg 源码和 LGPL relink 归档仍须补齐，不放宽门禁。

原生单一 UDP/FEC + WS/WSS、Web 保留 RTC 的产品决定不变。该检查点不代表整个 SDK 平台无关化已经完成。
