# Console 安全审计最终设计与验收计划

> 范围：远程访问、文件传输、设备资源告警、后台管理操作。
> 普通 `net_rtc` 当前尚未调通，本轮不纳入验收；`net_rtc_local` 及现有 Direct/UDP/Relay 链路仍在范围内。

## 1. 最终目标

安全审计必须形成一条可追踪、可恢复、不可被重复请求破坏的完整链路：

1. 事件开始时，Panel 先写本地 SQLite，再将开始事件写入持久化 outbox。
2. 事件正常结束时，更新同一条本地记录并写入结束事件。
3. Console 不可达时不丢事件；恢复后按退避策略自动补报。
4. Render、客户端或 Panel 异常退出时，记录必须在 5 秒宽限期或 Panel 下次启动时闭合。
5. Console 只允许 `running -> succeeded/failed/aborted`，重复请求幂等，禁止终态倒退和无开始记录的终态 upsert。
6. Web 同时展示访问记录、文件传输、设备告警和后台管理审计，并明确显示状态、结束原因及恢复标记。
7. 生产环境必须开启 Console 鉴权与 TLS；本地调试只能通过显式配置关闭鉴权。

## 2. 统一数据语义

### 2.1 状态

| 状态 | 适用记录 | 含义 |
|---|---|---|
| `running` | 访问、文件传输 | 已开始，尚未收到结束事件 |
| `succeeded` | 访问、文件传输 | 正常完成 |
| `failed` | 文件传输 | 已结束，但传输失败 |
| `aborted` | 访问、文件传输 | 进程退出、连接中断或启动恢复后被系统闭合 |

终态不可再次修改。完全相同的结束请求返回成功；内容不同的重复结束请求返回版本冲突。

### 2.2 结束原因

当前标准值：

- `client_disconnected`：远端客户端断开或文件发送端消失。
- `completed`：文件传输成功。
- `transfer_failed`：文件传输明确失败。
- `renderer_disconnected`：Render 进程断开且 5 秒内未重连。
- `panel_restart_recovery`：Panel 启动时发现上次遗留的未闭合记录。

`recovered=true` 表示终态由恢复流程生成，而不是业务端正常上报。

### 2.3 时间

- 所有时间均为 Unix 毫秒。
- Console 以已保存的 `begin` 和收到的 `end` 计算 `duration`，不信任调用方传入的耗时。
- `end` 不得早于 `begin`。

## 3. 已落地架构

### 3.1 Panel 本地记录与 outbox

- `visit_record` 和 `file_transfer_record` 保存状态、结束原因及恢复标记。
- `audit_outbox.event_key` 唯一，格式为：
  - `visit:<conn_id>:begin|end`
  - `file:<the_file_id>:begin|end`
- 上报成功后删除 outbox 项；失败后指数退避，最长 5 分钟。
- SQLite 操作固定在数据库线程，HTTP 请求固定在网络工作线程。
- 同一时刻只允许一个 outbox 请求在途，避免 Console 离线时任务无限堆积。

### 3.2 异常闭合

- Render 使用进程生命周期稳定的 `instance_id` 连接 Panel。
- Render 或客户端断开后保留 5 秒重连宽限期；同一实例恢复连接则不结束记录。
- 超过宽限期仍离线时，Panel 将该实例持有的访问/文件记录标记为 `aborted`。
- Panel 启动时扫描所有旧的未闭合记录，标记为 `panel_restart_recovery`。
- 恢复补报始终先重建 `running` 开始事件，再发送终态，避免 update-before-insert。

### 3.3 Console 状态机与索引

- 开始接口使用 `$setOnInsert`，重复开始不会覆盖已存在记录。
- 结束接口不再 upsert；开始记录不存在时明确失败并等待 Panel 重试。
- `conn_id`、`the_file_id` 建唯一索引；设备、状态和时间字段建查询索引。
- 连接 ID、设备 ID、方向、时间和字段长度均有边界校验。
- 上报使用 `x-px-appkey` 和 `x-px-device-id` 请求头；旧版 appkey 查询参数暂时兼容。
- 开始记录要求上报设备必须是访问/传输双方之一；结束记录根据数据库中的双方再次校验。

### 3.4 设备告警与后台操作

- CPU、内存、磁盘、GPU 使用率限制在 `0..=100`。
- 相同设备、相同告警类型及相同条件合并为一条记录，刷新最后发生时间并累计次数。
- 磁盘事件以设备、盘符和占用率作为同一条件；相同条件不会反复新增行。
- 后台管理审计记录真实授权 ID、动作、结果、目标及原因，不再使用固定操作者占位值。

### 3.5 Web 展示

- 安全审计页面包含访问记录、文件传输和管理审计。
- 访问与传输记录展示运行/成功/失败/异常、结束原因和恢复标记。
- 查询翻页保留当前过滤条件；表格支持横向滚动，避免窄屏字段截断。
- 管理审计支持按操作者、动作、结果和目标过滤。

## 4. 发布配置

生产配置要求：

- `environment = "production"`
- `force_authorize = true`
- `ssl_enable = true`
- 使用受信任证书或在部署设备上安装 Console 证书链。

`force_authorize = false` 仅用于明确的本地测试环境。Console 已在生产环境启动校验中拒绝关闭鉴权或 TLS 的配置。

测试阶段允许删除旧 SQLite/MongoDB 审计数据，不做历史数据迁移。若集合中存在旧重复 ID，应先删除测试集合，否则唯一索引创建会使 Console 启动失败。

## 5. 验收矩阵

| 场景 | 操作 | 通过标准 |
|---|---|---|
| 正常访问 | 建立连接后正常断开 | 一条记录从 `running` 变为 `succeeded`，时间正确 |
| Render 短暂重连 | 断开后 5 秒内恢复 | 原记录保持 `running`，不产生异常结束 |
| Render 崩溃 | 连接中 kill Render，等待超过 5 秒 | 记录变为 `aborted/renderer_disconnected` |
| Panel 崩溃 | 连接中 kill Panel 后重启 | 记录变为 `aborted/panel_restart_recovery` 且 `recovered=true` |
| 正常传输 | 完成文件传输 | `succeeded/completed`，耗时由 Console 计算 |
| 传输失败 | 中断或返回失败 | `failed` 或 `aborted`，不会显示成功 |
| Console 离线补报 | Console 离线时开始并结束，随后恢复 Console | outbox 最终清空，Console 只保留一条完整记录 |
| 重复请求 | 重复发送相同 begin/end | 结果幂等，不新增、不回退状态 |
| 冲突终态 | 对已结束记录发送不同终态 | 返回版本冲突，原记录不变 |
| 伪造设备 | 上报设备不属于记录双方 | 返回禁止访问 |
| 磁盘告警 | 连续上报同一盘符与占用率 | 页面只保留一行，次数和最后时间刷新 |
| 后台管理 | 管理员创建/修改/删除资源 | 审计操作者为真实 `auth_id` |
| Web | 查询、筛选、翻页及窄屏查看 | 条件不丢失，字段完整可读 |

## 6. 构建与发布检查

- [x] Rust Console 全量单元测试。
- [x] Web 类型检查与生产构建。
- [x] `px_panel` / `px_render` 增量 Release 编译。
- [x] 完成 `build_official.bat` 全部 975 个 C++ 构建步骤；停止占用旧发布文件的进程后完成 dist 收集，并分别完成 Console/Auth/Desk release 构建与部署。
- [x] 启动 Console、Panel、Render，验证正常结束、5 秒内重连、Render 断开、Panel 重启恢复和 Console 离线补报。
- [x] 验证 SQLite outbox 在 Console 离线时持久保留并退避重试，Console 恢复后自动清空；Console 幂等终态、冲突终态和磁盘事件聚合均通过真实 API 验证。
- [x] 验证 Console Web 首页与生产资源可访问；访问、文件传输和管理审计页面已通过类型检查及生产构建。
- [ ] 使用真实管理员会话人工复核管理审计筛选、窄屏横向滚动及操作者显示；自动化测试确认匿名查询仍返回 `AUTH_REQUIRED`。
- [ ] 用户确认后再提交和推送；本轮不主动提交。

## 7. 2026-08-21 自动验收结果

- 访问与文件传输 begin/end 重复请求均返回成功；不同终态返回 `VERSION_CONFLICT (635)`。
- 无 begin 的访问终态返回 `VisitNotFound (622)`；非法时间、方向和使用率返回 `InvalidParams (600)`。
- 同设备、同盘符、同占用率的磁盘事件保持相同 `event_id`，次数递增且最后时间刷新；占用率变化时生成新事件。
- Panel 重启恢复记录为 `aborted/panel_restart_recovery/recovered=true`。Console 停机期间 begin/end 均留在 outbox；Console 恢复后自动补报并清零。
- Render 同实例在 5 秒内重连不会被异常闭合；正常断开为 `succeeded/client_disconnected`。访问和文件传输超过 5 秒未重连均为 `aborted/renderer_disconnected`。
- 本轮按约定未验证普通 `net_rtc`；它仍是独立的待调通项，不影响上述审计链路验收结论。
