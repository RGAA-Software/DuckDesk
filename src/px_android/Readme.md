# Pixels Android Client

`src/px_android` 正在原地重建为正式的 Pixels Android 客户端。旧 GammaRay Android 应用不再维护，也不承担数据、API、UI、包名或安装升级兼容。

完整且具有约束力的产品、架构、删除范围、里程碑和验收标准见：

- [Pixels Android 客户端最终产品规划](../../docs/android_pixels_product_plan.md)
- [Pixels Android UI/UX 设计规范](../../docs/android_pixels_ui_design.md)

## 已确认的方向

- 产品显示名为 `Pixels`。
- 最终 namespace/applicationId 为 `yun.pixels.client`。
- 只发布 `arm64-v8a`，最低系统基线为 Android 10 / API 29。
- 使用 Kotlin、Compose、显式会话状态机和类型化 JNI。
- 复用项目协议、SDK 和媒体核心，删除旧 Fragment、GreenDAO、事件总线、JSON JNI、音乐频谱和 Steam 专属 UI。
- 最终产品包含音视频串流、完整输入、多显示器、远程应用、文件传输、剪贴板、录制、语音和完整传输能力。
- 不保留兼容层、迁移代码、旧入口或新版/旧版并行包。

## 当前状态

M0–M2 已完成。当前应用已包含 Pixels 品牌与最终包名、设备发现和扫码、Quick Connect、Console 账号与设备、远程应用、短期连接票据、前台会话服务、MediaCodec Surface 视频、AAudio、完整桌面输入、虚拟/实体手柄、多显示器、虚拟显示协议、双向文本剪贴板，以及基于 SAF 的双向文件传输和任务中心。

M3 的手柄震动与特定环境验收仍待收口；M4 后续继续实现图片/文件剪贴板、MediaStore 录制与语音；M5–M6 的完整网络矩阵和发布门禁尚未完成。未完成能力不会以占位实现伪装为可用。

构建基线为 Gradle 9.3.1、AGP 9.1.1、内置 Kotlin 2.4.10、Compose BOM 2026.08.00、API 37，最低系统 API 29。日常验证使用：

```powershell
cd src/px_android
./gradlew.bat testDebugUnitTest :app:assembleDebug :app:lintDebug
```

Debug APK 输出为 `app/build/outputs/apk/debug/app-debug.apk`。USB 安装和启动：

```powershell
adb install -r -d app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n yun.pixels.client.debug/yun.pixels.client.MainActivity
```

日常真机验证只做 `-r` 覆盖安装，不主动卸载或清空应用数据。`core-native` 打包 `pixels_android_core`，并通过 `RegisterNatives` 提供类型化 JNI；没有旧 JSON JNI 或 RTC stub。

后续 native C++ 聚焦验证使用仓库的 `build_cpp_android_*.bat` 入口。Windows release-only `build_official.bat` 不是 Android 开发命令。
