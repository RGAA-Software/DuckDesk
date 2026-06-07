# Guard 模块与 Rust 迁移

本文聚焦 `src/GammaRay/src/render_panel/guard`。这个模块不是 Windows Service，也不是普通后台脚本，而是运行在当前交互桌面会话中的 GUI 常驻守护进程。

## 当前 C++ Guard 的真实职责

- 单实例运行，锁文件名 `godesk_guard.dat`
- 初始化日志到 `GoDesk/gr_logs/godesk_guard.log`
- 注册登录计划任务 `GammaRay_Guard_Start`
- 创建一个透明、不可点击、置顶、10x10 的顶层窗口
- 每 5 秒检查 `GammaRay.exe` 是否存在，不存在就启动
- 每 5 秒检查 `GammaRaySysInfo.exe` 是否存在，不存在就启动
- 维护一个到 `/panel/renderer?from=guard` 的 WebSocket 连接骨架

其中隐藏窗口不是可随意删除的 UI 噪音，而是当前运行形态的一部分。Rust 迁移必须保留一个真实的顶层 Win32 窗口。

## Rust 重写目标

- 产物名仍为 `GammaRayGuard.exe`
- 仍运行在当前用户交互桌面
- 仍保留登录计划任务
- 仍保留隐藏顶层窗口
- 仍只守护 `GammaRay.exe` 和 `GammaRaySysInfo.exe`
- 保留 Guard 到 panel 的 WebSocket client 骨架

## 当前迁移落点

新的 Rust crate 位于 `rust/gr_guard`，按测试驱动推进：

- `config.rs`
- `logging.rs`
- `single_instance.rs`
- `hidden_window.rs`
- `task_scheduler.rs`
- `process_lister.rs`
- `process_monitor.rs`
- `process_spawn.rs`
- `panel_client.rs`
- `runtime.rs`
- `app.rs`

## 测试策略

Guard 迁移不是“功能写完再补测试”，而是每落一层能力就同时落测试。当前方案分为五层：

1. 单元测试：路径、常量、守护判定、计划任务参数、窗口样式
2. 组件测试：单实例、runtime 启停、spawn 调度
3. 集成测试：Guard 启动编排与模块组合
4. 桌面行为验证：隐藏窗口保留/去除的对比实验
5. 端到端替换验证：Panel 拉起 Guard，Guard 补拉 Panel/SysInfo

目标不是少量 smoke test，而是把 Guard 变成高覆盖、可重构的 Rust 模块。
