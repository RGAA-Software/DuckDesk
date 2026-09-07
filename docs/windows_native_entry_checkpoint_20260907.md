# Windows/Panel 固定原生入口：第四检查点

日期：2026-09-07；基于 `b77deacca` 继续实施。整个 SDK 精简计划尚未完成。

## 已实施

- 原件保存在 `backup/windows_native_entry`，79 个文件有 SHA-256 清单，不覆盖此前批次。
- 设置对话框去掉强制 Relay、Direct、RTC、UDP；保留音频、剪贴板、观看模式、分窗口、解码/捕获和现有调试设置。
- 对话框改用 Qt 父对象所有权与 `QPointer` 观察，保存回调不捕获裸 `this`，调用方使用独占所有权关闭后释放；同时移出原死代码表单。
- Panel 不再根据持久化通道标志或 Console ICE 的探测开关选协议。设备、应用和文件接入只尝试实际 Render 端点，失败不进入 Relay/RTC。
- 移除 Panel 的 RTC 超时回退状态机、重启入口和相关专属测试；不再向客户端传递通道、Relay/P2P 或 ICE 配置。
- Windows Client 固定 `kUdpDirect`，删除 `network_type`、`force_direct`、`enable_p2p`、`relay_host`、`relay_port`、`relay_appkey`、
  `signal_remote_device_id` 命令行选项，不再消费 `PX_RTC_ICE_CONFIG`。旧参数明确报错，不作为隐藏兼容入口接受。
- 密码直连（含带设备标识的分享链接）通过既有 `PrepareIpDirectLaunch` 取得 stream/nonce，再启动原生客户端。
- 独立文件会话使用服务端签发的运行时 stream ID，不自行生成另一个 ID；普通文件入口继续复用已连接客户端。
- 校验启动必需的 stream、ticket/直连凭据和 nonce。此校验只检查结构，实际鉴权仍由 Render 完成。
- 修复 UDP 分支强制开启音频的问题，尊重音频设置；文件模式仍不启用音视频。

## 编译与验证

```text
build_cpp_tests.bat px_client px_panel test_connection_policy test_stream_launch_auth_workflow
    test_stream_launch_child_arguments test_udp_media_state test_udp_media_failure
gradlew.bat :app:assembleDebug
```

Windows Client、Panel 及上述测试均编译成功，未运行 release-only 全量构建。
Android Debug 再次编译成功；本轮未修改 Android 源码、未安装或卸载手机应用。

7 组 CTest 全部通过，用时 13.19 秒：

- `common_message_notifier`、`common_async_runtime`
- `test_udp_media_state`、`udp_media_failure`
- `panel_connection_policy`、`panel_stream_launch_auth_workflow`、`panel_stream_launch_child_arguments`

覆盖凭据缺失、原生探测不受 ICE/Relay 指令改变、端点不可达、运行时身份、票据/nonce 传递，以及已有的取消、销毁和回调生命周期用例。
`scripts/test_windows_native_cli.ps1` 验证 7 个旧参数均以 Unknown option 拒绝。
`scripts/test_sdk_move_windows_smoke.ps1 -Seconds 20` 不再传入网络类型；本机默认 UDP 首帧、连续解码和窗口存活通过，无解码错误。
这不是完整 GUI 点击、远程账号应用、真实文件传输或音频播放验收。本轮测试总时长低于 5 分钟。

`check_cpp_ownership.ps1`、双端原生入口检查、SDK 路径检查通过；新设置表单和凭据测试通过 clang-format 检查。
本机日志为 `test-results/windows-native-entry-*`，CLI 日志为 `test-results/native-cli-*`，直连结果为 `test-results/sdk-move-windows-smoke.json`。

## Windows 交付

已使用发布脚本同步 Client、Panel、运行 DLL、皮肤和语言资源到 `build_official/dist`，逐项哈希一致。
发布时停止占用文件的进程，之后恢复 `px_service` 与 Panel；测试 Client 已退出。

- `px_client.exe`：`3F06CD584EBBABADA7C4844E825B7D44778FB21A13A81EA037049F34BA8DEC69`
- `px_panel.exe`：`133144139E1A2DBC89E4CBAB5671A0927839D862E306CC209396F11AF74FCDA6`

## 未完成范围

1. SDK 内部 RTC、Relay、旧 UDP/KCP、独立 WS 媒体的代码与构建依赖、Windows Client RTC DLL 包装仍存在，后续先核对媒体/语音消费者再归档。
2. SDK 与 Qt/平台适配的职责分离尚未完成。Web/Render RTC 本轮未改动，未重新测试浏览器功能。
3. 数据库中的旧通道列和共享数据模型字段暂留，本轮不删除用户数据或执行表重建；启动链路已停止消费这些值。
4. 真机、正式签名包覆盖安装、Release 合规归档输入仍按前序交接条件处理，当前不是最终候选版本。
