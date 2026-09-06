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

M0 已于 2026-09-05 完成：旧 Fragment/XML UI、GreenDAO、事件类、Steam/频谱页面、旧签名和旧 Android JNI/C++ 入口均已删除。当前内容包括 Pixels 品牌、Splash、设备首页、Quick Connect、设备空状态以及设备/传输/设置一级导航。最终清理 APK 已在 MIUI 真机完成覆盖安装、冷启动、视觉和崩溃日志检查。

M1 正在实施。当前 Quick Connect 已能解析私有网络主机、可选端口和 `link://`，真实请求 Panel `/v1/simple/info` 后保存设备；设备元数据由 DataStore 持久化，临时凭据使用 Android Keystore AES-GCM 加密。成功、不可达、无效响应、进程重启保守离线和应用内删除均已完成真机验证。扫码、主动局域网发现、会话鉴权、typed JNI 和远程画面仍待后续切片。

构建基线为 Gradle 9.3.1、AGP 9.1.1、内置 Kotlin 2.4.10、Compose BOM 2026.08.00、API 37，最低系统 API 29。日常验证使用：

```powershell
cd src/px_android
./gradlew.bat :core-domain:testDebugUnitTest :core-data:testDebugUnitTest :feature-devices:testDebugUnitTest :app:assembleDebug :app:lintDebug
```

Debug APK 输出为 `app/build/outputs/apk/debug/app-debug.apk`。USB 安装和启动：

```powershell
adb install --no-streaming -r -g app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n yun.pixels.client.debug/yun.pixels.client.MainActivity
```

日常真机验证只做 `-r` 覆盖安装，不主动卸载或清空应用数据。当前不打包项目 native 会话库，也不保留 RTC stub；typed JNI 和 `core-native` 将在 M1 后续切片建立。未完成动作会明确提示，不能视为远程会话已可用。

后续 native C++ 聚焦验证使用仓库的 `build_cpp_android_*.bat` 入口。Windows release-only `build_official.bat` 不是 Android 开发命令。
