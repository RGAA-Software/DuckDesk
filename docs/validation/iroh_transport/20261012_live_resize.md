# 原生窗口操作期间持续重绘

已有慢调用证据将两处约688/889ms显示空档对齐到 `WM_NCLBUTTONDOWN` 与 `WM_SYSCOMMAND` 嵌套处理，而接收线程仍前进。另一处4.25秒呈现空档缺窗口状态证据，仍不定因。

只读核对项目固定SDL源码 `src/px_deps/px_sdl3/src/video/windows/SDL_windowsevents.c`：进入Windows移动/缩放及菜单循环后设置10ms定时器，调用 `SDL_OnWindowLiveResizeUpdate`。`SDL_video.c` 在应用未使用MainCallbacks时发出 `SDL_EVENT_WINDOW_EXPOSED`，data1=1。旧主绘制循环在原生调用返回前不会消费该重绘，因此接收/解码健康时也会暂停显示。

项目新增 `LiveResizeRefresh` RAII事件观察器，只处理当前窗口、UI主线程的live-resize expose；使用弱引用恢复拥有者，并防止回调递归。主绘制与live-resize复用同一Frame绘制路径，绘制中再次收到重绘立即忽略；仍在UI线程使用现有ImGui/Vulkan/D3D资源。最小化时不绘制。渲染器只在实际像素尺寸改变时resize，避免每个定时重绘重建交换链。Run结束时先注销观察器，再销毁其状态；注销中回调的强引用保留状态到退出。

没有修改第三方SDL，没有新增后台渲染线程或改变原生窗口移动/缩放行为。此机制不是网络停顿修复。

## 回归

- live_resize_refresh验证其他窗口、普通expose与工作线程推送不触发绘制；嵌套事件不递归；派发中注销和销毁安全；重复注册/注销、注销后排队事件安全。
- video_presentation_progress检查重复重绘不伪造新视频帧。
- 初次专项及publish中的标题栏全局SendInput点击失败，第一次独立复测通过，记录均保留，未用复测冒称原始失败消失。改为对owned窗口发实际hit-test/capture/release消息，验证同一标题栏逻辑且避免输入落在操作者切换的前景窗口；最终三项均通过。
- 构建固定18 jobs。实机原生拖动及完整持续视频验收尚未完成，身份以status和构建SHA记录为准。
