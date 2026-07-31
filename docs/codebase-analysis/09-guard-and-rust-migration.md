# Guard 模块与 Rust 迁移

> 现状：`gr_guard`(GammaRayGuard.exe)已删除,保活职责并入 `gr_user_proxy`(GammaRayUserProxy.exe)。本文保留历史背景,并说明合并后的形态。

## 历史:C++ Guard 的真实职责

旧 `src/GammaRay/src/render_panel/guard` 是运行在当前交互桌面会话中的 GUI 常驻守护进程:

- 单实例运行,锁文件名 `godesk_guard.dat`
- 初始化日志到 `GoDesk/gr_logs/godesk_guard.log`
- 注册登录计划任务 `GammaRay_Guard_Start`
- 创建一个透明、不可点击、置顶、10x10 的顶层窗口
- 每 5 秒检查 `GammaRay.exe` 是否存在,不存在就启动
- 每 5 秒检查 `GammaRaySysInfo.exe` 是否存在,不存在就启动
- 维护一个到 `/panel/renderer?from=guard` 的 WebSocket 连接骨架

之后曾整体迁移到 Rust crate `rust_client/gr_guard`(产物 `GammaRayGuard.exe`,保留登录任务、隐藏窗口与 5s 保活语义)。

## 现状:保活并入 UserProxy

`rust_client/gr_guard` 已整体删除,保活逻辑收敛为 `rust_client/gr_user_proxy/src/keepalive.rs` 一个模块:

- 监控 `GammaRay.exe` + `GammaRaySysInfo.exe`,5s tick,启动时先做一次 initial check(SysInfo 缺失即拉起)
- 拉起 `GammaRay.exe` 优先走 `schtasks /Run /TN GammaRay_Panel_Start`(经 panel 已注册的登录任务,保持提升权限语义且免 UAC),失败回退直接 spawn
- `GammaRaySysInfo.exe` 直接 spawn(`Command` + `DETACHED_PROCESS`,`current_dir` 为 exe 同目录)
- 在 `app.rs` 的 `run()` 里以 `tokio::spawn` 5s interval 循环驱动,日志并入 `godesk_user_proxy.log`

随 Guard 消亡一并移除的:

- 隐藏窗口与 Win32 消息循环(UserProxy 是 tokio 进程,不需要)
- `panel_client.rs`(只收不发的占位 WS 连接,panel 端无特殊处理)
- 登录计划任务 `GammaRay_Guard_Start`(UserProxy 由服务以 session 用户 token 拉起并保活,不需要;旧任务由安装/卸载 NSIS 脚本 `schtasks /Delete` 清理)
- panel 侧的 `GrGuardStarter`(panel 不再守护任何进程)及 `GammaRayGuardIn/Out` 防火墙规则
- 各处 kill 列表、构建 target(`GammaRayGuard_rust`/`GammaRayGuard_stage`)、打包收集项中的 Guard

## 行为变化(与旧 Guard 相比)

- 保活循环从"提升权限的独立 Guard 进程"变为"session 用户态的 UserProxy 内嵌循环";panel 拉起走计划任务保持提升语义,SysInfo 不再默认提升(只做信息采集,影响可忽略)
- UserProxy 自身仍由服务每 3s 保活(`should_restart_user_proxy`),不变
- 进程名匹配不区分大小写,tick 决策"缺谁拉谁",均有单测覆盖(`cargo test -p gr_user_proxy`)
