# WebView 标准光标修复（2026-09-09）

## 原因与修复

CEF `OnCursorChange` 对标准光标只发送类型；只有自定义光标附带位图。
Render 已将类型和可见性写入 `CursorInfoSync`，但 Client 接收端直接要求有效位图，
导致手形、IBeam 等标准光标消息被忽略，保留初始箭头。

Client 现在先处理隐藏状态，然后处理无位图的标准类型，再处理自定义位图。
复用现有 `ToQCursorShape` 映射，无可用标准形状时回退箭头。
保留位图尺寸、热点和 DPR 校验；畸形位图仍被拒绝。
第一轮只改 Client；下述复验发现还需要隔离 Render 的宿主光标来源。
不改 CEF、传输协议、数据库或输入/剪贴板模块。
原有两级 UI 回调的 weak_ptr 生命周期检查保持不变，未新增订阅或异步持有者。

## 验证

- `client_cursor_image` 通过：标准类型无需位图、隐藏优先、64 次重复状态切换，
  以及已有高 DPI、像素所有权、非法位图/热点检查。
- C++ ownership 检查通过。
- helper 与测试文件 clang-format 检查通过；`ct_base_workspace.cpp` 整文件存在历史格式问题，
  未为此重排无关代码。
- 实际远端页面的悬停视觉验收仍需在更新后的 Client 上确认；单元测试不代表端到端验收。

## 交付

`scripts_build/build_cpp_client.bat` 增量编译成功，发布脚本同步 Client 运行产物并逐项校验哈希。
build-tree 与 `build_official/dist/px_client.exe` 的 SHA-256 一致：
`2EBABB20845668147CD581F8F660DC8E85293F4546203839E6C957589BACEB37`。

## 第二轮：实际运行仍为箭头（19:20–19:27）

用户复验第一轮无效。加入 Client `[CursorSync]` 接收/应用日志后发现：
客户端持续收到约 60 次/秒的 `type=0 size=32x32` 宿主桌面光标消息，反复应用箭头。
DDA 源启动时会独立启动光标采集循环；`OnCapturedCursorBitmap` 原来只检查退出状态，
未排除 WebView，导致宿主桌面光标与 CEF 光标竞争。

- Render：`OnCapturedCursorBitmap` 排除 WebView/RDP；WebView 保留独立 CEF `on_cursor` 发送路径。
- Client：有尺寸但无像素是桌面重复位图省略消息，保留已缓存光标；无尺寸无像素才按标准类型处理。
- 暂保留 `[CursorSync]` 诊断日志供用户复验；尚未做日志降噪，不应视为最终发布日志策略。
  收尾阶段用户已确认光标正常，上述逐消息/逐应用诊断日志已移除，避免桌面模式高频刷日志。

增量构建、同步与哈希校验完成：

- Client build/dist：`9B8D020CCB3F2113E1C56EB864013CBE019A8E849A824291D46835F5182BD37C`
- Render build/dist/90：`934A7A41BF240D2CE8425C1504410EBC326D29BC9749A1A4941D230556A52977`
- 90 原 Render 文件保留在 `C:\Program Files\PixelsRender\px_render.pre-cursor-20260909.bak`。
  采用原文件改名后放入新文件方式，桌面 Render PID 10748 未停止；未重启系统/服务或注销会话。
- 只停止旧测试实例 `inst-205-11d9426b`，新实例 `inst-208-65b09c39`，Client PID 30788 留给用户。

19:27 执行约 5 秒仅移动鼠标的检查。日志确认 CEF 类型 1/12/0（IBeam/Hand/Arrow）
分别到达客户端并应用 Qt shape 4/13/0，随后再次返回 IBeam；宿主 32x32 箭头持续消息消失。
这是实际端到端消息与 Qt 应用证据，最终视觉感受仍交用户确认；不扩展为其他输入/剪贴板已修复。
