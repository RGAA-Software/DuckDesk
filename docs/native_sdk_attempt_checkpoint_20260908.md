# P3a 会话尝试检查点（2026-09-08）

代码已实施；Windows 实连、双端编译及自动测试通过。最新 Android JNI 回调改动的手机界面回归待补：
手机被另一个项目占用后已停止操作，没有把其他应用导致的 UI 自动化失败算作 Pixels 故障或通过证据。

## 职责核对

- SDK 复用 `PxConnectionAttemptWorkflow` / `PxReconnectSupervisor`。WS/WSS 收包先检查当前 generation 和 Ready，
  再执行授权拒绝解析和业务分发；旧连接不能终止新尝试，占用/授权拒绝也不能继续接收后续业务消息。
- **运行中的 socket 重连**推进 generation、建立新连接适配器，不销毁整个 SDK 和已有文件任务。
  **用户重新启动已停止的会话**才创建全新 SDK；停止的对象不复活。
- Windows Panel 的启动授权工作流为每次启动申请新票，迟到 HTTP 结果通过工作流 generation 丢弃。
  Workspace 不拥有 Console 登录/续票能力，不能为了形式统一而给它复制一套账号状态机。
- Android 同一页面尝试需要保存最新轮换票据；续票提交后再次检查协程取消，取消期间返回的新票仍被保存，
  但不启动 JNI。实际 JNI 创建/句柄发布与取消清理由原生命周期锁和 NonCancellable 边界协调。
- Android 每次 JNI 创建使用独立 callback UUID，仅作为本地回调路由标识；认证 streamId、ticket、nonce 不变。
  普通回调属于该次尝试的 Job，Stop 先取消排队/挂起回调，再排空 Native 工作并移除绑定。
  录制 Completed/Failed 单独保留终结通知，由不可变 recordingId 匹配，不被普通会话取消吞掉。

平台账号与 UI 职责不同，不意味着需要两份网络状态机。共同协议/连接规则在 SDK，宿主只管理真实授权能力和生命周期。

## 本次证据

- Android JVM 18 项通过，`sdk-callback-attempt-final-android-build.log`：Debug APK 构建成功。
- 根构建 WS 重连、共享录制、Client 录制生命周期、RecordWriter 四组通过，10.16 秒，
  `sdk-attempt-final-ctest.log`。
- 独立 Windows SDK WS 重连八项通过，5.63 秒，`sdk-attempt-independent-reconnect-ctest.log`。
  包括真实服务端拒绝后禁止自动重试/后续业务，以及显式创建新连接后成功；该最后一条扩展尚未重新编译根测试副本。
- Windows 发布版本实际连接 20 秒通过，`sdk-attempt-final-windows-smoke.log`。
- 独立 Windows/Android 完整 SDK 均重新构建成功。手机最新 APK 已覆盖安装，未卸载；最新远控 UI 回归待手机空闲。

## 已发布版本指纹

构建树与 `build_official/dist` 对应产物 SHA-256 已核对一致，具体日志为
`sdk-attempt-final-client-publish.log` / `sdk-attempt-final-render-publish.log`。
以下为本检查点快照，后续构建会产生新指纹。

| 产物 | SHA-256 |
|---|---|
| px_client.exe | 6C54EB17897551AD3F6D40684BABD84F4379371A95CB63902984175A93CAFAB5 |
| px_render.exe | 75F0F4CEA7E4D1EDB8E1F8772D778724A3C53D2BF5D64AEBD390C495F0E239A7 |
| Android Debug APK | CE12DCB2C31729BA39C0BE1093E7F691CA973D0676452930CE7A65F5DC160C32 |

修改前工作区内容已归档到 `backup/native_sdk_attempt_ingress_20260908`、
`native_sdk_ticket_attempt_cancellation_20260908`、`native_sdk_android_callback_attempts_20260908`，均逐文件校验哈希。
