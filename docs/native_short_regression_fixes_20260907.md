# Native 短回归问题修复（2026-09-07）

## 范围与实现

本轮处理 Windows / Android 实测暴露的问题，不改公网部署、不引入传输回退，也不改动工作区已有的 Rust 服务端变更。
每个运行时测试场景控制在 5 分钟内，构建不计入测试时长。

| 问题 | 原因 | 修复 |
| --- | --- | --- |
| Android 下载块序号跳跃、Windows 大文件接收不完整 | 每个入站命令单独提交到阻塞线程池，互斥锁只能保证互斥，不能保证 FIFO | `FtAsyncSession` 使用单消费命令队列，命令与 Tick 顺序推进；发送 Prepare/Commit 期间不再穿插引擎修改 |
| 上传界面成功但接收端只留下 `.download` | 读端本地 EOF 直接触发成功；接收端错误未终止读作业 | 本地主动上传保留作业，等写端 SHA-256 校验、同步和最终 rename 后的 Done 回执；写端错误终止读作业；等待回执 30 秒超时报错 |
| Android 失败后重试误报解码器不可用 | 连接 stop 删除了仍在显示的 Surface 登记；现有 View 不会再次触发 surfaceCreated | Surface 登记由 View detach 和 transport close 清理，连接失败/重试不删除登记 |
| 连续第二次账号重试被拒绝 | 首次续票后仍保留旧请求中的一次性 renewal token | 每个会话保存最新票据与尝试状态，连续续票使用最新旋转令牌，close 清理 |
| 语音持续溢出、解码比例低 | 主动丢弃最老包后，下一播放周期又为同一包产生 PLC，没有消费队列，引发持续溢出 | 主动削减缓冲延迟时同步推进读取序号；保留真正网络缺包的 PLC 行为 |
| 断线后重新传输报“未完成 SHA-256 校验” | 新写作业打开首块前，误调用 ModifyTime 收尾前一个任务留下的 `.download` | 只有持有写入流的作业才收尾上一文件；新任务正常从头写入，继续保留 EOF SHA-256 校验 |

写端 Done 回执使用现有协议，不新增传输通道。只有本地主动上传读作业等待回执；远端请求的下载读作业保持原 EOF 行为，
下载接收端仍在本地校验和落盘后通知 UI。这样不要求现有 Web 下载实现增加 ACK，Web 上传端会忽略额外的终结回执。

`FtAsyncSession` 停止后为终态，重新连接创建新实例。停止会丢弃尚未运行的命令，回调内部停止不等待自身；
`PostAndWait` 拒绝在运行时/阻塞工作线程中同步等待，队列被清理时返回失败。
回调内停止时，由运行时的 join 协调器持有退出任务，等待已取消的协程收尾后再关闭运行时，避免 scope 析构额外等待 5 秒。
FIFO 与回调内退出重复 10 轮通过；最后一轮这两项合计 4 毫秒，已消除此前每轮约 5 秒的退出等待。

## 构建和自动测试

- 使用 `build_cpp_tests.bat` 增量构建 `px_client`、`px_render` 及相关测试，未运行 `build_official.bat`。
- Android `:core-domain:test`、`:core-native:testDebugUnitTest`、`:app:assembleDebug` 通过。
- 最终构建的 10 个 CTest 测试组通过，10.68 秒：9 组文件传输测试及 `voice_call_core`。
- 新增覆盖：1000 条命令顺序、阻塞回调内停止/丢弃排队命令、弱观察者失效、8 MiB 双异步会话传输、
  接收端回执到达前不能成功、接收端错误终止发送方、语音溢出恢复及序号回绕、10 次旋转票据续期。
- 所有权、SDK 目录、Native 单通道、Windows/Android 通道边界和 WebRTC DLL 链接边界检查通过。
- 修改区域按 `.clang-format` 格式化；全文件 dry-run 仍会报告未改动旧代码的格式问题，没有据此重排无关旧实现。

构建/测试日志保存在本机 `test-results/native-fixes-*.log`，不提交设备日志及运行时凭据。

### 异常路径补测

新增五项使用实际临时文件和双端引擎的测试：取消中途上传再重传、断线后重传、实际输出文件打开失败、
实际最终 rename 失败、等待完成回执时取消并收到迟到 Done。
断线后重传测试先复现了旧 `.download` 被误收尾的缺陷；修复后，相关四组 CTest 全部通过，耗时 5.57 秒。
断线时保留临时文件供重试，取消时清除当前临时文件，失败不会触发成功回调；重新传输后验证内容完全一致。
这些是可重复的引擎/实际文件系统异常测试，不冒充 USB 手机网络故障或磁盘耗尽实测。

## 实机结果

### Android 文件下载

USB 设备 `e2b3b128`（Android 14），使用 `adb install -r -d` 覆盖安装，未卸载或清空数据。
从本机 Render 下载原先失败的 `下载测试.bin`，通过 Android 系统文件选择器保存；UI 显示“已完成”。
2,097,152 字节，两端 SHA-256 一致：

```text
4CE1B3B92827036AD1A6632BB6B388D2C5F192C607E73FFAD287DBAB743A213E
```

### Windows 大文件上传

实际使用 `build_official/dist/px_client.exe`，通过远控菜单打开内置文件管理器，在本机不同目录间上传。
源文件 `Desktop/Pixels-FT-20260907-2107/A/cancel-retry.bin`，目标目录 `B/fix-2204/`。
536,870,912 字节，UI 显示 Success，目标正式文件存在且无 `.download/.digest` 残留，两端 SHA-256 一致：

```text
AA54B3854B2C3ECD98BA1DF99A086CA438975F0BD5C43DCB142470BCA206D5AE
```

本轮 Windows 场景结束于 22:07:29；原先失败的 `B/cancel-retry.bin.download` 和 `.digest` 未覆盖，保留用于比较。

### Android 语音

约 80 秒真实语音，静音、取消静音、挂断成功。停止统计：

| 指标 | 修复前 | 修复后 |
| --- | ---: | ---: |
| inbound | 4066 | 4126 |
| decoded | 605 | 3953 |
| jitter_missing | 3451 | 0 |
| jitter_overflow | 3451 | 163 |

消除了持续 PLC/溢出的循环；本轮仍有约 4% 的缓冲削减，不能声称零丢弃。
此结果是链路、缓冲和生命周期验证，不代替双端人工听音验收。

### Android 重试

初轮确认 Surface 修复后不再出现 DecoderUnavailable，但连续重试发现旧续票令牌复用。
补充最新令牌状态及自动测试后再次实测：Windows 占用时 Android 连续多次重试均正常收到占用拒绝，
没有再复用失效的续票令牌；释放 Windows 客户端并等待服务端现有 5 秒重连保护期后，在原页面点重试成功。
22:15:40 后恢复 `60 帧/秒 · 3 毫秒 · 丢包 0.0% · MediaCodec hardware`，不需要退出重进；随后正常结束会话。

## Windows 交付产物

已同步到用户启动目录 `build_official/dist`，构建树与 dist 的 SHA-256 一致：

```text
px_client.exe  43004165317579981A931477FE3A9633CBCA1B9E3636D97D1002E54F6420063A
px_render.exe  45B65908672B5BA587E024EFDFE478ED9F2ABBE6937E054B1346F771DC032B55
```

发布脚本同时核验了语音 APM DLL、语言资源、Render 图标和两份 WebRTC DLL。发布占用文件时停止对应进程，随后恢复 `px_service`。

最终 Debug APK 已覆盖安装到 USB 手机，SHA-256：

```text
7780586CCAE97EFEFB02EA6DF0C1ADF84D4CD995915DBDB033FD676724F67314
```

512 MiB 传输和约 80 秒语音实测发生在回调退出时序及断线重传修复前；后续修复不改变文件协议及语音算法，最终版本已重跑所有上述 CTest 测试组。
最终 dist 另跑 20 秒 Windows 启动冒烟：窗口、UDP 首帧和连续解码均通过，没有解码错误；测试结束后关闭测试客户端。

## 归档

原实现及测试原始字节按批次保存在 `backup/`，每批有基线提交、路径、工作区状态、原因与 SHA-256 清单：

- `native_ft_order_ack_20260907`
- `native_voice_overflow_20260907`
- `native_android_retry_surface_20260907`
- `native_android_retry_owner_20260907`
- `native_android_rotating_ticket_20260907`
- `native_ft_callback_drain_20260907`
- `native_ft_exception_regression_20260907`
- `native_ft_interrupted_restart_20260907`

未做蜂窝公网测试，未改变设备安全设置。锁屏/后台/网络切换的完整验收以及人工听音不由本轮短回归代替。
