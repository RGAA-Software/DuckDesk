# SDK 网络参数隔离检查点（2026-09-08）

## 已实现

上一检查点拆开构建目标，本轮解除共享网络代码对会话/解码参数的依赖。

- 新增 `SdkConnectionParams`，只使用标准库值类型，包含端点、媒体启用状态、路径、连接身份和短期授权信息。
  不包含 FFmpeg、D3D11、Vulkan、Surface、Qt、显示设置或传输选择枚举。
- `NetClient` 按值接收配置，保存为不可变快照，不再持有 `ThunderSdkParams` 或依赖调用方后续修改。
  身份/路径不再重复保存为另几份可变字段。一次新的授权尝试创建新客户端，不通过修改旧快照换票。
- `ThunderSdk::Init` 在会话装配边界显式投影网络所需值。Windows/Android 的上层调用无需同时进行大规模 UI/解码资源迁移。
  这是配置投影，不是旧传输的兼容层，也没有新增网络通道。
- 空 UDP 关联码在 `NetClient` 构造时生成并由该实例持有；显式提供的关联码继续保留。
  不再回写调用方共享参数。该值仍只用于关联已认证 WS 与 UDP 媒体，不替代鉴权。
- `Connection`、WS、WSS、UDP 原本完全不读取所持有的 SDK 参数，现已去掉该成员和构造参数。
  下层连接只接收自身所需的通知器、端点和路径。
- `px_sdk_core` 不再链接 `px_common` 总库，仅使用消息、异步、文件和加密/FEC 等已有公共组件。
  会话/平台目标仍按需使用桌面公共组件。没有调整第三方 FFmpeg/libwebrtc ABI。
- WS 重连、UDP 故障和新增配置测试直接链接 `px_sdk_core`；文件 E2E 也不再链接完整 SDK，
  但文件测试自身仍有 Qt/文件引擎依赖，不能称作整个测试集无 Qt。

## 回归与门禁

新增配置测试覆盖默认值、调用方清空配置后的地址/鉴权/心跳身份保持、临时配置销毁、
显式 UDP 关联码、三个新会话各自生成关联码、退出并销毁后再次发送排队计时消息。
既有 WS/UDP 测试继续覆盖重连、重复启动/停止、回调内停止、分发时注销以及观察者失效。

`sdk_source_sets` 递归检查共享源文件引用的 SDK 内部头文件，禁止间接引入 `sdk_params.h`、
`thunder_sdk.h`、解码器类型或 `gl/` 帧/显示类型；同时保留平台清单互斥、完整性和未实现平台报错检查。
该门禁检查 SDK 内部依赖，不声称已经清除了 asio2/公共组件内合理的操作系统适配。

验证结果：

- Windows `build_cpp_tests.bat` 构建 Client 和相关 SDK 测试通过；未运行 release 全量入口。
- Android `:core-native:testDebugUnitTest :app:assembleDebug` 通过；`e2b3b128` 覆盖安装成功并启动应用，未卸载/清数据。
- 最终 7 组 CTest 全部通过，14.76 秒：源码/头依赖边界、连接配置、UDP 媒体状态、AVFrame 所有权、
  UDP 故障、码流辅助、WS/WSS 重连。新增配置组包含 4 项测试。
- 文件 E2E 目标仅编译，本轮未配置它的外部鉴权/目标参数，不计入上述 7 组，也不宣称本轮重做了真实文件传输。
- Windows Ninja 实际链接图确认配置、UDP 故障、WS 重连测试不包含 `px_sdk.lib`、平台解码对象、
  FFmpeg、D3D11/DXGI、Qt Core 或 `px_common_win`；不是仅凭目标命名判断隔离。
- `build_official/dist` 20 秒本机直连冒烟通过：窗口、UDP 媒体、连续解码正常，无解码错误；测试客户端已退出。
- SDK/双端原生传输、WebRTC DLL 边界及 C++ 所有权门禁通过。新文件通过完整 clang-format 检查；
  旧文件保留无关历史布局，全文件 dry-run 仍报告旧命名空间/缩进等格式问题，未据此重排整个连接实现。
- `dist` 客户端、语音 APM DLL、语言资源与构建来源 SHA-256 一致，发布时恢复 `px_service`。
  APK 与手机实际安装的 base.apk SHA-256 一致。本轮未重建 Render 或重测浏览器 WebRTC。

```text
px_client.exe 49DF783114AE267F211719DCFEFC70E7C62C7D90C7EA6DC80B88F3FBC95D55BD
Debug APK     558ED03FBB52CD9D2FD87970D64EFB8B8497DE1C1424106FBDF45E37DD1B7BA1
```

本机日志位于 `test-results/sdk-connection-params-*.log`，不提交设备日志、配置或运行时凭据。

## 尚未完成

- `ThunderSdkParams` 仍是现有客户端会话/显示装配参数，硬件上下文尚未迁出；网络层不再依赖它，不代表该类型本身已平台无关。
- 具体解码工厂、Surface 交接、CPU/GPU 帧资源所有权，以及可脱离工作区独立配置的完整 SDK 入口仍待收口。
- iOS/macOS 尚未实现。原生公网 P2P/Relay、统一能力协商仍按既定决策暂缓。
- 不重做人工听音、锁屏/网络切换完整验收或蜂窝公网测试；Release 归档输入要求不变。

原文件完整保存在 `backup/native_sdk_connection_params_20260907`。批次开始于跨日前，包含上一轮未提交的构建改动，
清单记录修改前完整内容、工作区状态和 SHA-256，不从 Git HEAD 重建旧文件，也不参与产品构建。
