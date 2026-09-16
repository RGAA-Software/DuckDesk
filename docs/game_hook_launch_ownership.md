# Game Hook：仅允许本产品启动的 App

用户决定（2026-09-09）：不考虑 Steam App；只处理本产品启动的 App，不接管用户手动运行的游戏。

## 准入规则

Hook 前必须同时满足：**本次启动的私有 Windows Job 成员 AND 完整 exe 路径匹配**。

- 根进程以 `CREATE_SUSPENDED` 创建，成功加入私有、不可继承句柄的 Job 后才恢复执行。
- 普通 App 只允许根进程；配置 `game_view_path` 的启动器场景允许该 Job 中完整路径匹配的子进程。
- 发现候选、实际注入、Hook bootstrap 和看门狗恢复都重新检查归属；注入期间保留目标进程句柄，防止 PID 复用。
- 同名、同路径、用户手动重启的外部进程均不接管。关闭与恢复只操作本次拥有的 Job，不按进程名或历史 PID 杀进程。
- 不走 Steam URL / `ShellExecute` 启动和扫描接管分支。外部 broker 代启动但不属于该 Job 的进程也不接受。
- 原 game/webview 用户环境不增加 RDP 登录前置条件；RDP 的账号、会话保留策略不变。

## Windows 路径与参数

- 配置、Console、Service、Render 和日志中的可执行文件路径都保留 UTF-8、空格、非 ASCII 字符和原始参数边界。
- Windows 启动处只使用宽字符 API。`lpApplicationName` 单独传入完整 exe，命令行中的 exe 另加引号；参数尾串原样传入。
- 比较实际进程句柄查询到的完整路径，不依赖进程快照的文件名。短文件名、符号链接或目录联接别名不自动视为同一路径；不确定时拒绝。

## 停止与退出

- Service 对已经 `Stopped` 的实例立即成功确认，不查询旧 PID/端口、不再次发送停止消息，也不操作替换进程。
- Console 对已经 `Stopped` 的实例直接返回当前快照，不改变版本和停止时间，不要求 Service 在线。
- 真正的 `Failed` 仍按失败处理，不通过错误字符串把所有失败掩盖为成功。
- 正常停止只使用记录的 Render PID，同时核对 Render 完整 exe、App 模式和端口；游戏子进程由该 Render 的私有 Job 收尾。
- Service 的进程观察句柄绑定启动 request ID 和该代内核对象；PID 复用不得改变观察目标。
- Console 持久化 `stop_reason` / `exit_code`；迟到心跳不得复活已确认终态的同代实例。

| 情况 | `stop_reason` | 状态 / error |
|---|---|---|
| 无客户端宽限退出 | `no_clients` | stopped / 空 |
| 启动宽限内无客户端 | `startup_idle` | stopped / 空 |
| 其他退出码 0 | `clean_exit` | stopped / 空 |
| 其他已观察退出码 | `abnormal_exit` | failed / `RENDER_EXIT_XXXXXXXX` |
| 没有可靠退出码 | `process_lost` | stopped / `PROCESS_LOST` |
| 显式停止成功 | `requested_stop` 或 `already_absent` | stopped |

## 验收要求

- 自动化必须覆盖同路径外部实例拒绝、错误路径拒绝、根/子进程准入、空格与 Unicode 路径、停止互不影响、回调内停止和重复启停。
- 公网实机验收只使用 Console 当前配置的节点与应用描述，不写死机器、路径、端口或设备 ID。
- 实机必须分别验证 Hook 图像、音频、输入、自然退出、显式停止、重复停止和外部同名进程不受影响。
- 构建树与 `build_official/<product>/dist` 中所有变更运行产物的 SHA-256 必须一致后才能交付。

旧实现完整归档位于 `backup/game_hook_owned_process_20260909/`、`backup/app_stop_idempotency_20260909/` 和
`backup/app_exit_observation_20260909/`，仅供参考，不参与构建、打包或运行。
