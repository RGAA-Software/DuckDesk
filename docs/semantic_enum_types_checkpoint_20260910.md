# 消息与数据类型枚举整理（2026-09-10）

## 范围

本批处理的是表达“种类、状态、操作”的裸数字，不是尺寸、时长、序号或所有整数。基于 `ceecb52dc`，不修改协议编号，也不修改生成的 Protobuf/Prost 字段。

| 位置 | 改动 |
| --- | --- |
| 共享 SDK 解码器接口及 Windows/Android 实现 | 编码使用 `VideoType`，图像格式使用 `EImageFormat`；未初始化状态使用 `std::optional`，移除 `-1` 类型哨兵。 |
| Client 剪贴板消息 | 使用 `ClipboardType`，不再先保存为整数、发送时强转。序列化路径采用智能指针和同步引用。 |
| Panel 会话及内部事件 | 会话类型使用可选 `CpSessionType`，拒绝原因使用 `CpTransportRejection`，语言使用 `LanguageKind`。 |
| Panel 设置复选框 | 双态控件直接订阅 `toggled(bool)`，不再比较 `state == 2`；本次调整的回调保持安全生命周期捕获。 |
| Render 内部消息 | 客户端类型使用 `ClientType`，编码帧类型使用 `VideoType`，显式映射编码器事件类型。 |
| Android JNI 语音状态 | 使用具名 `JavaVoicePhase`，只在 JNI 调用边界转换成 `jint`。 |
| Web | 集中定义消息、文本状态/结果、编码、游戏状态、虚拟屏操作/结果及客户端类型枚举；业务分支使用名称。原有 `MSG_TYPE_*` 导出保持可用。 |

协议枚举优先复用已有定义。Web 枚举是当前 Protobuf 动态加载方式的类型层，新增测试逐项核对其线格式数值，避免两份定义漂移。

## 异常值与生命周期

- 解码器初始化拒绝不支持的编码和非法图像格式；本批不增加 VP9 支持。
- Android MediaCodec 记录图像格式，并在格式变化时参与重建判断。
- Panel Hello 必须包含合法会话类型；未握手连接不默认视为某个有效会话。
- 剪贴板发送拒绝未知类型。Web 未知编辑状态按 Unknown 处理，未知屏障结果不能进入编辑状态。
- SDK 回归覆盖初始化失败后清理、重复停止、排队回调销毁及回调中停止等既有生命周期用例。

## 原件与边界

- 修改前的 30 份完整原件保存在 `backup/semantic_enum_types_20260910/`；`manifest.json` 记录路径、基准提交、原始修改状态和 SHA-256，已逐项核对。
- 归档仅供参考，不进入编译、测试、打包或运行目录。
- 未修改现有 Rust 工作区改动；未改第三方源码或既有插件 ABI。
- 未做协议升级、传输重构、iOS/macOS 适配，也不宣称已清除整个仓库的全部语义裸数字。

## 验证记录

- 独立 Windows SDK 编译通过；`sdk_decoder_factory`、`av_frame_ownership` 两项 CTest 通过，共 11.20 秒。
- Android `:core-native:testDebugUnitTest :app:assembleDebug` 通过；18 项 JVM 测试零失败。未连接手机进行本批功能验证，未安装、卸载或清除手机数据。
- Web `npm test` 通过：53 项 Vitest 测试及 19 项语音断言；`npm run build` 通过。5 个 Web 构建文件已同步到 `build_official/dist/web_client`，SHA-256 一致。
- C++ 新增代码所有权/格式门禁通过。
- Windows `px_client`、`px_panel`、`px_render` 及对应测试目标增量编译通过。初次编译发现设置页 `QPointer` 捕获变量位于内层作用域，已移到构造函数作用域并重新编译通过。
- Windows `sdk_decoder_factory`、`client_clipboard_module_lifecycle`、`client_clipboard_file_stream` 三项 CTest 全部通过，共 13.79 秒；每项超时上限为 45 秒。
- Client、Panel、Render、RTC/语音/RDP 运行库、主题及语言资源已按发布脚本同步到 `build_official/dist`，源文件与目标文件 SHA-256 一致。本地服务已恢复运行。
- 发布后的 20 秒本机只读连接检查通过：窗口创建、UDP 媒体接收、连续帧解码均成功，未发现解码错误。此检查不代表已完成设置页全部交互、真实剪贴板互通或手机端功能回归。

本地验证日志位于 `test-results/semantic-enums-*.log`；不将日志或本机配置作为源码交付内容。

本机首次验证拉取后的 Windows 代码时缺少固定版本 FreeRDP SDK，已按仓库现有脚本准备。FreeRDP 基准为 `aa8650b300aa4cabd85d9c72b431301509b9043f`，只在 `.cache` 隔离构建副本应用仓库已有审核补丁，原始检出保持不变。未绕过 SDK 校验，也未运行 release 全量构建。
