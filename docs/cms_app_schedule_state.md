# CMS 应用调度：状态同步与联调修复

> 状态：已落地（2026-08，commit `dc64453c`）  
> 范围：同机多实例端口、启停回执、状态恢复、路径校验、Web 列表展示  
> 相关计划：[`cms_app_schedule_plan.md`](./cms_app_schedule_plan.md)

---

## 1. 背景与问题

P3 调度链路打通后，本机 E2E（CMS Web + console Service + game-hook Render）暴露出几类「状态与现实不一致」问题：

| 现象 | 实际原因 |
|------|----------|
| 开两个应用，第二个 Client 空白 / `ERR_EMPTY_RESPONSE` | `plugin_ssl_proxy` 在 `listen_port+1` 绑 HTTPS，与下一实例 HTTP 冲突 |
| 点启动失败，页面无明确错误 | HTTP 立刻返回，未等 Service `StartAppInstanceResult` |
| 游戏/Render 已停，页面仍「停止中」+ `unknown instance_id` | Service 重启后本地注册表为空，CMS 仍 Stopping；unknown 被当成失败 |
| 进程已死，页面仍「运行中」 | Service 心跳继续上报已死实例；CMS 对账认为仍存活 |
| 页面粘住「失败 / game_exe_rel must be relative」 | 历史 Failed 实例被 Web 当成当前行状态；或 `game_exe_rel` 误存绝对路径 |
| 启动报游戏不存在 | 路径空格不一致（如 `CarGame 汽车` vs `CarGame  汽车`） |

目标：CMS 列表状态反映 Service/进程真相；启动失败可 toast；同机多实例端口互不踩踏。

---

## 2. 架构要点（现状）

```
CMS Web (/apps)
    │  HTTP start/stop/list（带 appkey）
    ▼
gr_cms_server · AppScheduleManager
    │  WSS /spvr/service
    │  Start/Stop + HeartBeat(instances_json)
    ▼
GammaRayService · AppInstanceRegistry
    │  起停 GammaRayRender(--app_mode=game-hook)
    ▼
游戏进程 + WebRTC /web_client
```

路径模型：

```
保存应用：game_path（绝对 exe）
  → split → install_root + game_exe_rel（仅文件名）
启动：resolve_start_paths(game_path, placement.install_root, game_exe_rel)
  → Service resolve_game_path → 绝对 game exe
```

端口：

- CMS 默认池起点 `32000`，保存应用时按已占用端口 **+1** 建议下一端口。
- Service 本机池 `32000–32999`；CMS 下发 `listen_port>0` 时按首选端口占用。
- **已移除** `ssl_proxy`：Render 不再额外占用 `listen_port+1` 做 HTTPS 反代。

---

## 3. 状态机与恢复规则

### 3.1 Instance 状态

| 状态 | 含义 |
|------|------|
| `starting` | 已下发 Start，等待回执 |
| `running` | Start 成功，有 listen_port / pid |
| `stopping` | 已下发 Stop，等待回执 |
| `failed` | Start/Stop 明确失败（持久化，但 Web 不粘显示） |
| `stopped` | 已停止或对账清除 |

### 3.2 Service 侧

1. **心跳前 `reap_dead_app_instances`**  
   对 `running`/`stopping`：若该 `listen_port` 上已无 game-hook Render → `mark_stopped`。  
   若端口上仍有进程但 pid 变了 → 校正 pid。

2. **心跳 `instances_json`**  
   上报本机注册表摘要（含 state）。`render_alive` = desktop 存活 **或** 仍有 starting/running/stopping 应用实例。

3. **Stop 遇 `unknown instance_id`**  
   视为已停止成功回执（注册表本就没有，无需再杀）。

4. **Start/Stop 异步化**  
   独立 tokio 任务 + `tokio::time::sleep`，锁只在状态迁移时持有；`mark_running` 拒绝终态记录、`mark_stopped` 拒绝 `starting`（防并发踩踏）。

5. **kill 前 pid 身份校验（`pid_belongs_to_instance`）**  
   目标 pid 必须是该实例端口上的 game-hook Render 或游戏 exe，防 pid 复用误杀；身份全不符返回 Err 且不 `mark_stopped`。kill 后按端口 1s 复查，Render 仍在 → Err 保留端口，CMS 标 Failed。

6. **端口校验与探测**  
   实例端口限定 `[32000, 32999]`；spawn 前 `TcpListener` bind 探测 OS 占用。

### 3.3 CMS 侧

1. **`reconcile_from_service_hb(device_id, instances_json)`**  
   - `instances_json` 解析失败：warn + **跳过本轮对账**（不再按空列表清空 Running）  
   - CMS 上该机器的 `running`/`stopping`  
   - 若 **不在** HB 的「活跃」集合中 → 标 `stopped`，清 pid/error  
   - 活跃集合：HB 条目 `state` ∈ `{running, starting, stopping}`；`state` 为空（旧版）仍视为活跃  
   - HB 里同 id 但 `state=stopped` **不能** 保住 CMS 的 Running  
   - **双向对账**：HB 活跃条目对应 CMS 实例为 `stopped`/`failed` 时 → 恢复 `running` 并回填 pid/listen_port（迟到 ok 回执不复活 Failed，靠此恢复）

2. **Service WebSocket 断开**  
   不再立即空列表对账：延迟 15s（`DISCONNECT_RECONCILE_DELAY`）后才以 `instances_json=[]` 对账，清粘住的活跃态；期间 Service 重连（epoch 变化）则取消本次对账，避免重连抖动误清 Running。

3. **Stop 结果**  
   `unknown instance_id` / `unknown instance` → 标 Stopped（不清成 Failed）。

4. **Stop 时 Service 已离线**  
   直接标 Stopped，避免永久 Stopping。

5. **Start HTTP**  
   注册 oneshot，最多等 **25s** Service 回执；Failed 时 HTTP 返回错误文案供 Web toast。

6. **`resolve_start_paths`**  
   优先用绝对 `game_path` 拆分；兼容旧数据里 `game_exe_rel` 误存绝对路径的情况。

### 3.4 Web 侧（`AppsView.vue`）

列表行**只绑定** `running` / `starting` / `stopping` 实例。  
历史 `failed` / `stopped` 不作为当前行状态（启动失败靠 toast；避免「失败」永久粘在表格上）。

---

## 4. 已删除：ssl_proxy

| 项 | 说明 |
|----|------|
| 插件 | `src/gr_render/plugins/ssl_proxy/**` 已删 |
| 注册 | `plugins/CMakeLists.txt`、`plugin_ids.h`、`kSSLProxyPluginId` |
| 原因 | HTTPS 监听 `listen_port+1`，与「下一实例 HTTP = 当前+1」冲突 |
| 后果 | 同机多实例可连续使用 32000、32001、32002…；Client 走 HTTP `http://ip:port/web_client/...` |

---

## 5. 关键代码位置

| 模块 | 文件 | 职责 |
|------|------|------|
| CMS 调度 | `rust_server/gr_cms_server/src/app_schedule/manager.rs` | 启停、路径拆分、对账、Start 等待 |
| CMS HB | `.../net_service/spvr_service_conn.rs` | 心跳触发 `reconcile_from_service_hb` |
| CMS 断线 | `.../net_service/spvr_service_ws_handler.rs` | 断开后延迟 15s 空列表对账；期间重连则取消 |
| Service 注册表 | `rust_client/gr_service/service_core/src/app_instance.rs` | 端口池、路径、状态 |
| Service 起停 | `rust_client/gr_service/src/service_host.rs` | 起停 Render、`reap_dead_app_instances` |
| Service 心跳 | `rust_client/gr_service/src/cms_client.rs` | HB 前 reap；Stop unknown→成功 |
| Web | `web/gr_cms/src/views/AppsView.vue` | 列表绑定、启停 toast |
| Render | `src/gr_render/rd_app.cpp` | 游戏启动失败快速失败（无 ssl_proxy） |

---

## 6. HTTP API（Web 实际使用）

| Method | Path | 说明 |
|--------|------|------|
| GET | `/api/v1/app/control/app/rows` | 应用+放置合成行 |
| GET | `/api/v1/app/control/app/next-port` | 建议下一端口 |
| POST | `/api/v1/app/control/app/save` | 创建/更新应用与放置（单表单） |
| POST | `/api/v1/app/control/app/delete/{app_id}` | 删除 |
| POST | `/api/v1/app/control/app/instance/start` | 启动（阻塞至回执或超时） |
| POST | `/api/v1/app/control/app/instance/stop/{instance_id}` | 停止 |
| GET | `/api/v1/app/control/app/instance/list` | 实例列表 |

Client URL：

```
http://{device_ip}:{listen_port}/web_client/?deviceId={device_id}&instanceId={instance_id}
```

---

## 7. 本机 E2E 要点

### 7.1 组件

| 组件 | 典型路径 / 参数 |
|------|------------------|
| CMS | `output/gr_cms_server/gr_cms_server.exe --running-mode=server`（HTTPS `:30500`） |
| Service | `scripts\service_test_ctl.bat start`（封装 `build_official/dist/GammaRayService.exe --console --port 20375`） |

Service 启停脚本（console 模式，2026-08-08 新增）：

```bat
scripts\service_test_ctl.bat start          rem 新窗口前台起，默认端口 20375
scripts\service_test_ctl.bat start 20376    rem 指定端口
scripts\service_test_ctl.bat status         rem 查看是否在跑
scripts\service_test_ctl.bat stop           rem 停止
```

- 工作目录固定 `build_official\dist`，日志直接打在 console 窗口。
- SCM 服务方式（开机自启）不需要脚本，用 dist 里的 `GammaRayServiceManager.exe install --service-bin <path>` / `stop` / `query` / `remove`。
| 鉴权注入 | `node scripts/inject_service_auth.mjs --device-id e2e-machine-1 --appkey … --spvr-host 127.0.0.1 --spvr-port 30500` |
| Render | 与 Service 同目录的 `GammaRayRender.exe` |

### 7.2 路径注意

- Windows 路径中的**连续空格**必须与真实目录一致（资源管理器复制最稳妥）。
- 保存时应填**游戏 exe 绝对路径**；CMS 会拆成 `install_root` + 文件名。

### 7.3 验收清单

1. 保存两个应用，端口分别为 32000、32001（或 CMS 建议值）。  
2. 依次启动：两个 Render/游戏均起来；`netstat` 可见对应端口 LISTEN。  
3. 「打开」两个 Client，页面均非空。  
4. CMS 停止 → 状态到已停止；进程退出。  
5. 外部杀掉 Render/游戏 → 约一个心跳周期（~5s）内 CMS 从「运行中」变为「已停止」。  
6. 重启 Service 后对仍显示 Stopping/Running 的旧实例 → 心跳对账清成 Stopped。  
7. 故意错误路径启动 → toast 明确错误，列表不永久粘「失败」。

### 7.4 相关单测

```bash
# CMS（51/51 通过）
cargo test -p gr_cms_server -- \
  stop_unknown_instance_marks_stopped \
  reconcile_clears_stale_running \
  reconcile_ignores_stopped_hb_entries \
  resolve_start_paths_heals_absolute_exe_rel \
  reconcile_skips_on_bad_json \
  reconcile_revives_stopped_and_failed_from_hb \
  late_start_result_does_not_revive_failed \
  start_result_device_mismatch_ignored \
  heal_instance_after_restart_fixes_transitional_states \
  save_app_rejects_port_out_of_range \
  remove_conn_keeps_newer_connection

# Service（service_core 51/51、gr_service 32/32 通过；含 pid 复用误杀防护、token 边界匹配、端口校验等新测试）
cargo test -p service_core
cargo test -p gr_service
```

---

## 8. 运维与排障

| 症状 | 检查 |
|------|------|
| 机器下拉为空 | Service 是否在线；是否已 `inject_service_auth`；CMS `/api/v1/service/control/query/all/service/conn` |
| 一直运行中但无进程 | Service 是否为含 `reap_dead_app_instances` 的版本；看 conn 里 `instances_json` |
| 一直停止中 | Stop 回执 / HB 对账；Service 是否连上 CMS |
| `game_exe_rel must be relative` | 重新「编辑保存」应用（写绝对 game_path）；或确认已部署 `resolve_start_paths` |
| 端口被占用 | `instance/list` 是否仍有活跃实例占端口；或 OS 上其它进程占端口 |

部署后 Web 需 **Ctrl+F5**，避免旧前端仍粘历史 Failed。

---

## 9. 明确不做 / 后续

- 不做 CMS 推包安装（仍只登记路径）。  
- 不做自动调度 / ready 探测（仍属计划 P5）。  
- 公网 HTTPS 入口若需要，应另做统一入口，而不是恢复实例级 `ssl_proxy` 抢 `port+1`。  
- 可选增强：Failed 写入应用级 `last_error` 短暂展示；Starting 超时自动 Failed。

---

## 10. 2026-08-08 修复

### 10.1 CMS 侧（rust_server/gr_cms_server）

1. HB `instances_json` 解析失败：warn + 跳过本轮对账（不再按空列表清空 Running）。
2. `remove_conn` 改 compare-and-remove（`Arc::ptr_eq`）；`add_conn` 顶掉旧连接时主动 close 旧连接，修复重连踩踏导致 Start/Stop 误报 offline。
3. Service WS 断开不再立即空列表对账：延迟 15s（`DISCONNECT_RECONCILE_DELAY`），期间重连（epoch 变化）则取消。
4. 双向对账：HB 活跃条目对应 CMS 实例为 Stopped/Failed 时恢复 Running 并回填 pid/listen_port。
5. `load_from_db` 修复：`starting` → `failed("CMS restarted")`、`stopping` → `stopped`。
6. Start 创建实例即写入期望 `listen_port`（并发 Start 预占检查生效）。
7. `delete_app` 同步删除 Mongo 实例记录（`delete_instances_by_app`）。
8. 端口统一校验 `[32000, 32999]`。
9. Start 超时清理 `request_index`；迟到 ok 回执不复活 Failed（靠 HB 双向对账恢复）；终态清理 `request_index`；回执校验 `device_id` 一致。
10. query 参数日志脱敏（token/appkey/secret 只打前 8 位）。
11. 测试：`cargo test -p gr_cms_server` 51/51 通过（新增 `reconcile_skips_on_bad_json`、`reconcile_revives_stopped_and_failed_from_hb`、`late_start_result_does_not_revive_failed`、`start_result_device_mismatch_ignored`、`heal_instance_after_restart_fixes_transitional_states`、`save_app_rejects_port_out_of_range`、`remove_conn_keeps_newer_connection`）。

### 10.2 Service 侧（rust_client/gr_service）

1. kill 前 pid 身份校验（`pid_belongs_to_instance`：必须是该实例端口的 game-hook Render 或游戏 exe），防 pid 复用误杀；身份全不符返回 Err 不 `mark_stopped`。
2. kill 后按端口复查（1s 窗口），Render 仍在 → Err，保留端口占用，CMS 标 Failed。
3. spawn 后 4s 查不到 pid 的超时分支：先按端口查找并杀树兜底再 `mark_failed`。
4. reap 覆盖 Failed 实例（端口有活 Render 则杀树清记录）。
5. Start/Stop 异步化：独立 tokio 任务 + `tokio::time::sleep`，锁只在状态迁移时持有；`mark_running` 拒绝终态记录、`mark_stopped` 拒绝 Starting（防并发踩踏）。
6. 端口校验 `[32000, 32999]` + `TcpListener` bind 探测 OS 占用。
7. `find_game_hook_pid_by_port` 删除 contains 子串分支（3200 误中 32000），只留 token 边界精确匹配。
8. HB summaries 只报活跃状态；终态记录 10 分钟 TTL 清理（`prune_finished`）。
9. CMS URL 日志脱敏；重连退避 2s→30s。
10. 测试：`service_core` 51/51、`gr_service` 32/32 通过（含 pid 复用误杀防护、token 边界、端口校验等新测试）。

### 10.3 第二轮追加（2026-08-08）

- `suggest_next_port` 封顶：超 32999 回落池起点扫描第一个空闲端口，全满报「端口已用完（32000-32999）」；handler 适配 Result 返回。新增 3 单测（`gr_cms_server` 54/54）。
- `port_bindable` 补 IPv6 `[::]` 探测：`AddrInUse` 才算占用，无 IPv6 栈不误伤。新增 1 单测（`service_core` 52/52）。
- 实测备注：Windows 上 Rust std 未设 `SO_EXCLUSIVEADDRUSE`，「先绑具体地址再绑通配」可双绑成功——仅绑回环具体地址的占用探测不到，但此时 render 自己 bind 通配同样成功，行为一致。

### 10.4 force_authorize 测试开关（2026-08-08）

- `gr_cms_server_settings.toml` 新增 `force_authorize`：`false` = WS token 过滤（client/panel/service/website）与 HTTP appkey 过滤一律放行（本机/测试）；缺省（不写）为 `true` 强制鉴权。
- 测试部署（`output/gr_cms_server/`）已置 `false`；生产部署应显式 `true`。
- 放行时 `/spvr/service` 不再需要 appkey/token 参数即可连接（`inject_service_auth` 不再是前置条件）。
- WS token 过滤器的拒收类单测现在显式置 `force_authorize=true`（结构体 `Default` 为 false）；新增 `client_bypassed_when_force_authorize_false`。
