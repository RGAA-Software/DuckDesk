# Native SDK 独立源码集成检查点（2026-09-08）

> P4 已完成本轮构建、接入及局部回归；P3 业务规则、语音 UDP 和 P5 最终验收仍未关闭。
> 基线 `e90892046`，改动未提交、未 push。接入说明见 `src/px_client_sdk/README.md`。

## 实施

- SDK 可直接配置或由独立消费工程 add_subdirectory；自行定位同 checkout 的依赖，不要求调用者提供 PX_PROJECT_PATH。
- 提供 `pixels::sdk` 和 `pixels::transport` 两个消费目标；Windows Client、Android JNI 改用相同的 `pixels::sdk`。
- `PX_SDK_CORE_ONLY` 不配置平台解码器、Opus 或 FFmpeg；独立示例主动检查目标闭包，无 Qt、RTC、Relay、HTTP/设置/桌面工具。
- SDK-only 测试不查找 Qt；宿主已有文件 UI harness 单独启用，现有 Windows 根工程仍默认保留该测试。
- 为 px_common 增加宿主组件开关，补齐公共 include/link 传播；SDK 与 Opus 只依赖实际需要的公共组件。
- 将 Windows 设备名/client type 的填写从 ThunderSdk 移回 BaseWorkspace。新增测试保证 SDK 不覆盖宿主身份。
- 归档退出未启用的 libyuv RGB 转换与无用 D3D debug include，不为独立构建拉入无用媒体依赖。
- 增加 `scripts_build\build_cpp_sdk_standalone.bat` 和 `examples/lifecycle`；不调用 release、npm、Gradle 或 Rust 构建。
  示例使用真实 SDK/平台工厂，离线创建/初始化、重复退出 3 轮，不请求账号或建立伪连接。

## 验证

| 项目 | 结果 |
|---|---|
| Windows 独立 full | 编译/链接通过；离线示例通过，0.80 秒 |
| Windows 独立 core | 编译/链接通过；最终离线示例通过，0.32 秒 |
| Android NDK 独立 full/core | 均通过；最终再构建 no work to do |
| Android 独立运行 | 两个 executable 均在 USB 手机执行，输出 `Pixels SDK offline lifecycle OK` |
| Windows 独立工程 7 组 CTest | 全通过，27.16 秒；强制禁用 Qt 查找 |
| 当前 Windows 应用 | 增量目标构建通过，最终 `px_client check_cpp_ownership` 通过 |
| 当前 Android Debug | `:app:assembleDebug` 通过，2 分 49 秒 |
| 应用构建树 8 组 CTest | 全通过，27.67 秒；解码工厂现有 10 个用例 |
| Windows dist 实连 | 20 秒通过，窗口、UDP 和解码正常，无解码错误 |
| Android APK 实连 | 覆盖安装并核对 hash；58–59 fps 硬解，文件页返回 59 fps，正常结束 |
| 所有权/目录/单通道/设置/JNI 脚本、diff check | 全通过 |

日志在 `test-results/sdk-independent-*.log`。中间曾出现独立示例构造 API 拼写、CMake link signature 问题，均已修正。
独立 Windows 链接还遇到间歇性输出文件无法打开；改用单次 manifest 链接后最终验证通过，未停止未知进程或调整安全软件。
Windows 发布后首轮实连早于本地 Render 就绪而失败；确认进程和监听端口就绪后，最终 20 秒实连通过。
手机首次自动点击早于主页加载，未进入连接；等待主页加载后完成上述短测，没有卸载或清数据。

## 交付

Client、APM DLL 和三份语言资源已同步到 `build_official/dist` 并逐项核对 SHA-256。
最终增量验证后再次核对 Client 构建树/dist 一致。本地 `px_service` 已恢复运行。

```text
px_client.exe
770C70ABCEC4F5156815466EAFFF25338B35DEC68C170DB30EAB95D881EC0186

px_voice_apm.dll
21411B44D9C7C8E6F270B6E932EF48C1511E542D0EA36BE25F37578ECAE669B3

app-debug.apk（安装后 base.apk 相同）
4C3A40AEABF84BE679BBB7B326A1BB03EC25B7EB3F8DE3C54B92701C931D1A0A
```

备份保留修改前工作区字节，均再次验证 manifest：

- `backup/native_sdk_independent_build_20260908`：9 文件。
- `backup/native_sdk_consumer_targets_20260908`：2 文件。
- `backup/native_sdk_independent_guard_20260908`：2 文件。

独立构建树通过 `/build_sdk_*/` 忽略，不提交编译产物；未动已有无关 Rust 业务改动。

## 边界与后续

首版是同 checkout 的源码集成，不是零依赖 SDK、稳定 ABI 或预编译包交付。
不支持的平台明确拒绝；Windows 静态 CRT 和 Android NDK/API/ABI 的验收范围见 README。
离线消费示例不验证网络首帧，不能替代另行完成的应用短实连。
本轮没有完成语音听音、录制/剪贴板完整功能回归或 Web 首帧；这些仍归 P3/P5。
P3 归属核对与发现的录制队列风险见 `native_sdk_business_rules_audit_20260908.md`，不以构建通过关闭业务收尾。
