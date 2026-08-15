# CMS 多机应用调度计划

> 状态：P3 已落地（2026-08）；状态同步与联调修复见 [`cms_app_schedule_state.md`](./cms_app_schedule_state.md)  
> 目标：CMS Web 配置应用与机器放置，向多台 Service 下发启停；Service 起停 game-hook Render 并回执；可打开 WebRTC Client。  
> 依赖：game-hook 出画/音频/inner 输入已打通（见 [`game_hook_capture_plan.md`](./game_hook_capture_plan.md)）。

---

## 1. 已拍板决策

| # | 决策 | 取值 |
|---|------|------|
| 1 | 应用分发 | 一期 **只登记路径**，不做 CMS 推包安装 |
| 2 | 同机同应用多实例 | **允许**（`instance_id` + `listen_port` 区分） |
| 3 | 调度策略一期 | **人工选机**（Web 下拉在线 Service） |
| 4 | Client 身份 | **机器 `device_id` + `instance_id`**（不伪造设备码） |
| 5 | 路径 | **`install_root` 每机可不同**；`game_exe_rel` 在 Application 模板全局一致 |
| 6 | 与 desktop 关系 | CMS **只管 game-hook 应用实例**；Panel desktop 单槽保留、互不误杀 |

---

## 2. 概念模型

```
Application（目录/模板）
  app_id, name, mode=game-hook
  game_exe_rel, default_game_args, render_profile
        │
        │  1:N
        ▼
AppPlacement（放置）
  placement_id, app_id, device_id, install_root
        │
        │  调度 1:N
        ▼
AppInstance（运行）
  instance_id, app_id, device_id, placement_id
  state, listen_port, pid, error
```

- **Machine**：现有 `device_id`；在线 = Service 已连 CMS `/cms/service`。
- **同应用调度到不同机器**：同一 `app_id`，不同 `device_id` 的 Placement；分别 Start 产生多个 Instance。

路径拼装（Service 本机）：

```
game_path = join(install_root, game_exe_rel)
→ Base64(UTF-8) → --app_game_path
GammaRayRender.exe 仍来自 GoDesk 安装目录（work_dir），与应用目录分离。
```

---

## 3. 端到端流程

```
CMS Web：选 Application + 在线 Machine → Start
        │
        ▼
CMS：校验 Service 在线 + Placement → Instance=starting
     WSS 下行 StartAppInstance(request_id, …)
        │
        ▼
Service：拼路径 → 分配/使用 listen_port → 起 Render(game-hook)
     上行 StartAppInstanceResult(ok/port/pid|error)
        │
        ▼
CMS：Instance=running|failed
Web：「打开 Client」→ URL 含 device_id + instance_id（及 port/信令入口）
```

Stop 对称：只杀目标 `instance_id`，不影响同机其它实例与 desktop。

---

## 4. 协议（`cms_service.proto`）

在 Hello / HeartBeat 之外增加：

| 方向 | 类型 | 用途 |
|------|------|------|
| CMS→Service | `kCmsServiceStartAppInstance` | 启动 |
| Service→CMS | `kCmsServiceStartAppInstanceResult` | 回执 |
| CMS→Service | `kCmsServiceStopAppInstance` | 停止 |
| Service→CMS | `kCmsServiceStopAppInstanceResult` | 回执 |
| HeartBeat 扩展 | `instances_json` | 本机实例摘要（多机仪表盘） |

`request_id` 全链路关联，防多机回执串台。

---

## 5. 分期

| Phase | 内容 | 验收 |
|-------|------|------|
| **P0** | 本文档 + 决策 | — |
| **P1** | Proto + Service/CMS 编解码与命令分发（可 mock 进程） | 单测覆盖多机/多实例/失败 |
| **P2** | Service 真实起停 game-hook + 实例表 | 本机脚本级联调 |
| **P3** | CMS 持久化 Application/Placement/Instance + HTTP + Web 页 | CMS Web 启停 |
| **P4** | 多机联调 + Client URL | A/B 机同应用 |
| **P5** | 自动调度、ready、公网入口 | 后置 |

本期已落地（代码 + 单测）：

- Proto：`cms_service.proto` Start/Stop + Result + HB `instances_json`
- Service：`service_core::app_instance` 注册表/端口/路径/启动参数；`ServiceRuntime::start/stop_app_instance`；`cms_client` 收令回执
- CMS：`app_schedule` Application/Placement/Instance；Mongo `c_app` / `c_app_placement` / `c_app_instance`；HTTP `/api/v1/app/control/...`；WSS 下发与结果回调
- CMS Web：`/apps` 应用调度页（创建应用/放置、启停、打开 Client）
- Client URL：`http://{device_ip}:{listen_port}/web_client/?deviceId=...&instanceId=...`

### HTTP API（需 appkey 过滤）

| Method | Path | 说明 |
|--------|------|------|
| POST | `/api/v1/app/control/app/create` | 创建 Application（低层） |
| GET | `/api/v1/app/control/app/list` | Application 列表 |
| GET | `/api/v1/app/control/app/rows` | Web 用：应用+放置合成行 |
| GET | `/api/v1/app/control/app/next-port` | 建议下一 listen_port |
| POST | `/api/v1/app/control/app/save` | Web 用：创建/更新应用+放置 |
| POST | `/api/v1/app/control/app/delete/{app_id}` | 删除应用 |
| POST | `/api/v1/app/control/app/placement/create` | 机器放置（低层） |
| GET | `/api/v1/app/control/app/placement/list` | 放置列表 |
| POST | `/api/v1/app/control/app/instance/start` | 调度启动（等 Service 回执） |
| POST | `/api/v1/app/control/app/instance/stop/{instance_id}` | 停止 |
| GET | `/api/v1/app/control/app/instance/list` | 实例列表 |

### 单测覆盖场景

- 路径拼接 / `..` 拒绝 / Base64 game-path
- 端口池分配、冲突、耗尽、释放复用
- 同机多实例、停 desktop 不杀 game-hook、停 game-hook 不杀 desktop
- CMS 同应用两机 Placement；离线/无 Placement 失败；Start/Stop Result 状态机
- Proto Start/Stop 编解码 round-trip
- CMS Web：`web_client_url` game-hook URL 拼装
- 状态恢复：`unknown instance_id`→Stopped、HB 对账、忽略 HB 中 stopped 条目、绝对路径治愈

后续：真实多机联调（P4）、自动调度 / ready / 公网入口（P5）。  
联调问题与状态同步细则：[`cms_app_schedule_state.md`](./cms_app_schedule_state.md)。

---

## 6. 风险

1. Service 离线时调度失败，需明确错误码。  
2. `install_root`/`game_exe_rel` 拼错 → Start 失败回执。  
3. 端口冲突 → Service 分配端口并回报。  
4. 停应用勿杀 `--app_mode=desktop`。  
5. Proto 变更后需同步 rebuild `protocol` crate（CMS + Service）。  
6. Render/游戏被外部杀死后，依赖 Service 心跳 reap + CMS HB 对账，否则列表会短暂假「运行中」。  
7. 勿再引入实例级占用 `listen_port+1` 的反代（曾与同机多实例冲突）。
