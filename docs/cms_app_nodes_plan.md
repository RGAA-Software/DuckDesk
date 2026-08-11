# CMS 应用调度重构：应用-节点（流路）结构设计

> 状态：**已实现**（2026-08-11）。UI 文案定名为「节点」（列表列名、按钮「新建节点」、默认名「节点1/节点2…」）。
> 前置文档：[`cms_app_schedule_plan.md`](./cms_app_schedule_plan.md)、[`cms_app_schedule_state.md`](./cms_app_schedule_state.md)
> 范围：gr_cms_server 调度模型 + CMS Web `/apps` 页；Service/Render 协议不变
> 实现备注：多开时同名游戏进程的 hook 目标选择在 render 侧修复（`app_manager_win.cpp`：优先选自己拉起的进程树 / 未被注入的实例），否则第二个实例会锁到第一个实例的进程上导致无画面。

---

## 1. 背景与问题

现模型是 `Application → AppPlacement(机器) → AppInstance(运行)`，端口挂在 **Application** 上（保存应用时建议一个 `listen_port`）。但业务上**应用需要多开**（同一应用同时跑多个流路），现模型的问题：

| 问题 | 原因 |
|------|------|
| 多开没有一等概念 | 多开只能靠反复 Start 同一 app+device，端口靠"建议下一端口"人肉错开 |
| 端口归属错误 | 端口是运行资源，却绑在应用模板上；第二个实例的端口无处可登记 |
| 多开状态无法区分 | Web 列表行只能绑"最新活跃实例"，同一应用多个实例看不出谁是谁 |
| 启动语义模糊 | "启动应用"到底用哪台机器、哪个端口？现在隐含在 placement + app.listen_port 里 |

## 2. 核心决策

| # | 决策 | 取值 |
|---|------|------|
| 1 | 新概念 | **AppNode（节点/流路）**：应用下的一路可运行单元 |
| 2 | 端口归属 | 从 Application 下沉到 **Node**（同机节点间端口唯一） |
| 3 | 机器归属 | 节点绑定 `device_id` + `install_root`（取代 Placement） |
| 4 | 应用启动语义 | **自动选一个空闲节点** → 生成该节点的 Instance → 启动 |
| 5 | 节点启动语义 | 直接在该节点上起 Instance（节点同时只允许 1 个活跃实例） |
| 6 | 选节点策略（一期） | 设备在线 + 无活跃实例的节点中，选**最久未运行**（其次序号最小） |
| 7 | Service/协议 | **不变**：仍按 instance_id + listen_port 起停，CMS 下发字段不变 |
| 8 | 旧数据 | 启动时自动迁移：旧 Placement + app.listen_port → 每个应用一个默认节点 |

## 3. 概念模型

```
Application（应用模板，不再持有机器/端口）
  app_id, name
  game_path / game_exe_rel, default_game_args
  encoder_fps / bitrate / format, webrtc_enabled ...
        │ 1:N
        ▼
AppNode（节点 / 流路）
  node_id, app_id, name            —— 如 "节点1" / "上海-1"
  device_id, install_root          —— 运行在哪台机器、哪个目录
  listen_port                      —— 本节点固定信令/媒体端口
  last_run_at                      —— 最近运行时间（选节点用）
        │ 1:N（历史；同一时刻 0 或 1 个活跃）
        ▼
AppInstance（运行）
  instance_id, node_id, app_id, device_id
  state, listen_port(=node.listen_port), pid, error
```

- **多开 = 多建节点**。想开 3 路戴森球，就在应用下建 3 个节点（可同机不同端口，也可不同机）。
- **外层"启动应用"** = 找一个空闲节点 → 产生一个该节点的 Instance → 启动。再点一次 = 再开一路（选下一个空闲节点）；全部占用 → 报错"没有空闲节点"。
- **节点状态不落库**：由该节点的活跃 Instance（running/starting/stopping）推导，复用现有对账/恢复逻辑。

## 4. 启动流程

### 4.1 节点启动（Web 点节点行的「启动」）

```
POST /api/v1/app/control/app/node/start/{node_id}
  校验:节点存在 / device 在线 / 该节点无活跃实例 / 端口未被同机其它活跃实例占用
  → Instance{node_id, listen_port=node.listen_port, state=starting}
  → WSS StartAppInstance(字段与现协议一致)
  → 等回执(25s) → running|failed        ← 与现有 start_instance 相同
```

### 4.2 应用启动（Web 点应用行的「启动」）

```
POST /api/v1/app/control/app/instance/start   {app_id}        ← 路径不变,语义改
  candidates = nodes(app_id) 过滤:device 在线 ∧ 无活跃实例 ∧ 端口可预占
  空 → 412 "没有空闲节点(全部在运行或机器离线)"
  选 last_run_at 最老(从未运行优先),平手取 node 序号最小
  → 之后的流程与 4.1 完全相同(创建 Instance → 下发 → 等回执)
```

停止不变：按 `instance_id` 停；节点行的「停止」停其活跃实例。

### 4.3 并发预占

创建 Instance 即写入 `node_id + listen_port`，后续启动检查以此为预占依据（沿用现有"创建即写期望端口"的做法），防两个并发启动选中同一节点。

## 5. 端口与校验

- 节点池仍 `[32000, 32999]`；**唯一性按 `device_id`**：同机两节点不可同端口（保存时校验，含与其它应用节点的冲突）。
- `next-port` 建议逻辑改为：给定 `device_id`，建议该机空闲端口（节点保存表单用）。
- 应用删除 → 级联删节点与实例记录；节点删除 → 仅当其无活跃实例。

## 6. HTTP API 变化

| Method | Path | 说明 |
|--------|------|------|
| GET | `/api/v1/app/control/app/rows` | 行 VO 增加 `nodes: [NodeVo]`（含各自活跃实例摘要） |
| POST | `/api/v1/app/control/app/save` | 只保存应用模板字段（去掉 device_id/listen_port） |
| POST | `/api/v1/app/control/app/node/save` | 创建/更新节点（name/device_id/install_root/listen_port） |
| POST | `/api/v1/app/control/app/node/delete/{node_id}` | 删除节点（有活跃实例时拒绝） |
| GET | `/api/v1/app/control/app/node/list?app_id=` | 节点列表（可选，rows 已带） |
| POST | `/api/v1/app/control/app/node/start/{node_id}` | 节点启动 |
| POST | `/api/v1/app/control/app/instance/start` | **语义改为自动选节点**（body 只需 app_id；旧 device_id/listen_port 字段忽略） |
| GET | `/api/v1/app/control/app/next-port?device_id=` | 按机建议端口 |
| GET | `/api/v1/app/control/app/launch/{app_id}` | **启动链接（应用级）**：自动选空闲节点并启动，成功 303 跳转到 `http://{节点机IP}:{节点端口}{web_client_hint}`；启动失败返回 JSON 错误 |
| GET | `/api/v1/app/control/app/node/launch/{node_id}` | **启动链接（节点级）**：指定节点启动，成功 303 同上；节点离线/已在运行返回 JSON 错误 |
| 其余 | instance/stop、instance/list | 不变（instance 增加 `node_id` 字段） |

launch 路由说明：GET 是为了让链接可直接放进浏览器地址栏/收藏夹；请求会挂起等待启动回执（≤25s，浏览器表现为转圈）；跳转目标 IP 取节点机的 `get_ip_from_link()`，失败回退 `127.0.0.1`；`web_client_hint` 为空时自动拼 `/web_client/?deviceId=..&instanceId=..`。鉴权与现有接口一致（`?appkey=`），链接明文携带 appkey，属既定模型。

`AppRowVo` → 应用视图；新增 `AppNodeVo{ node_id, name, device_id, listen_port, install_root, last_run_at, instance?: {instance_id,state,error,pid} }`。

## 7. Web（`/apps` 页）

- 应用表格新增 **「节点」列**（首列 expand）：展开为该应用的节点子表：
  `节点名 | 机器(在线?) | 端口 | 状态 | 错误 | 操作(启动/停止/打开/编辑/删除)`
- 应用行操作：**启动**（自动选节点，成功 toast 显示命中节点与端口）、**链接**（复制应用级启动链接 `/app/launch/{app_id}`，浏览器打开即自动选节点启动并跳转 web client；无节点时禁用）、**新建节点**、编辑、删除。
- 节点行操作：**启动/停止/打开 Client**（URL 与现规则一致，端口=节点端口）、**链接**（复制节点级启动链接 `/app/node/launch/{node_id}`；离线或运行中禁用）、编辑、删除。
- 启动链接统一为 `http://{CMS主机}/api/v1/app/control/app/...launch...?appkey=...`，复制后可发给他人或放收藏夹。
- 「打开」仍按活跃实例的 `listen_port` 拼 URL，逻辑不变。
- 应用行状态列改为聚合：`3节点 · 1运行 2空闲`。

## 8. 数据迁移与兼容

1. `load_from_db` 增加一步：对没有节点的应用，按旧 `AppPlacement`(device_id+install_root) + `app.listen_port` 生成默认节点 `节点1`，写库；旧 `c_app_placement` 集合迁移后不再写入。
2. `AppInstance` 增 `node_id`（`#[serde(default)]`，旧记录为空串）；对账/恢复逻辑只认 `instance_id`，不受影响。
3. 旧活跃实例（无 node_id）停止后自然消亡；Web 对无节点实例不再展示行。
4. Application 的 `listen_port`/`device_id` 字段保留反序列化（读旧数据），保存时不再写。

## 9. 分期

| Phase | 内容 | 验收 |
|-------|------|------|
| N1 | manager：AppNode 模型 + Mongo `c_app_node` + 迁移 + 单测 | 迁移/端口唯一/选节点策略单测过 |
| N2 | 启动链路：node/start + instance/start 改自动选节点 + 预占 | 并发/无空闲节点/离线单测过 |
| N3 | HTTP handler/router + rows VO | API 手测 |
| N4 | Web：节点列 + 节点子表 + 表单改造 | `/apps` 页多开 E2E |
| N5 | 联调：同应用 3 节点同机多开 + 跨机 | 验收清单全过 |

单测沿用现有框架（`cargo test -p gr_cms_server`），新增：默认节点迁移、同机端口冲突、选节点（最久未用/平手序号/全忙报错）、节点删除保护、并发双启动不撞节点。

## 10. 明确不做 / 后续

- 不改 Service、不改 `spvr_service.proto`（下发字段已够用）。
- 不做节点级资源配额/优先级调度（P5 自动调度一并考虑）。
- 不做节点分组/标签；节点名手工填。
- `install_root` 仍沿节点保存（一期不做 CMS 推包）。

## 11. 风险

1. **旧前端 + 新后端**：rows VO 结构变化，部署后 Web 需 Ctrl+F5。
2. **迁移幂等**：默认节点生成必须按 (app_id, device_id, listen_port) 去重，CMS 重启不重复建。
3. **端口静态绑定节点**后，Service 侧"端口被 OS 占用"仍可能失败 → 走现有 failed 回执 + toast，不自动换端口（端口是节点的身份，静默换端口会让 Client URL 不可预期）。
4. 选节点只看"无活跃实例"，不探测机器负载；多机负载均衡属 P5。
