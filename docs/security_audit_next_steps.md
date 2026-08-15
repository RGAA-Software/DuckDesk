# 安全审计（访问记录 + 文件传输记录）后续工作清单

> 本文档汇总安全审计功能剩余的所有工作，覆盖 P2 字段透传、运行时验证、数据迁移、端到端测试、安全加固及发布计划。
> 当前状态：P0/P1 修复已完成并通过编译 + CMS API 自测，尚未提交。

---

## 1. 目标与范围

让 CMS 安全审计页面能够**完整、准确、可恢复**地展示：

1. 每一次远程访问（连接类型、起止时间、耗时、访问方/被控方设备 ID）。
2. 每一次文件传输（文件名、方向、起止时间、耗时、成功/失败状态）。
3. 在 panel/service 异常退出后，再次启动时能把未闭合的记录自动闭合并同步到 CMS。

本文档只聚焦**访问记录 + 文件传输记录**这一垂直链路，不包含通用授权、RBAC、Relay 日志等。

---

## 2. 已完成项回顾（无需重复做）

| 模块 | 已完成内容 |
|------|-----------|
| C++ Panel | `WsPanelServer` 解析 render/client 的 begin/end 消息，写入 SQLite 并上报 CMS。 |
| SQLite | `visit_record.conn_id`、`file_transfer_record.the_file_id` 增加 `unique()` 约束。 |
| SQLite | 操作符 insert 改为 `replace`（upsert）。 |
| C++ Panel | 启动时扫描未闭合记录（`ScanUnclosedRecords`），闭合后同步 CMS。 |
| Rust CMS | MongoDB `c_visit` / `c_file_transfer` 创建唯一索引。 |
| Rust CMS | insert 改为 `replace_one(...).upsert(true)`。 |
| Rust CMS | update 使用 `find_one_and_update(...).upsert(true)`。 |
| Rust CMS | `total_size` 与查询使用同一 filter，分页总数正确。 |
| Web | 文件传输表新增「传输结果」「传输耗时」列；修复 `end==0` 显示为 `-`。 |
| Web | 搜索时重置页码；修复 `console.error` 文案。 |
| 编译 | `GammaRay.exe`、`px_cms_server` Release、Vue 前端均构建通过。 |
| API 自测 | insert/update/upsert/update-before-insert/query-total 均 200。 |

---

## 3. 剩余工作详单

### 3.1 P2：字段补齐与真实数据透传

#### 3.1.1 UDP / RTC / P2P 连接类型字段

**问题**：`RpClientConnected.conn_type` 是字符串，由 render 端填写。需要确认 UDP、RTC、P2P、Relay 等场景下该字段是否**始终有值且命名统一**，否则安全审计页面无法正确区分连接类型。

**待做**：

1. 在 render 端找到生成 `RpClientConnected` 的位置，打印或检查以下场景的取值：
   - Relay 模式
   - 直连（P2P）成功
   - UDP 打洞
   - RTC 通道
2. 如果存在空字符串、大小写不一致、或同一类型多个别名，统一为一套枚举值，例如：
   - `"Direct"`
   - `"Relay"`
   - `"P2P"`
   - `"RTC"`
   - `"UDP"`
3. 若需要强类型，可在 `px_render_panel_message.proto` 中将 `conn_type` 从 `string` 改为枚举，但需同步改 render、panel、CMS model，影响面较大，建议仅在字符串取值层面统一。

**涉及文件**：

- `src/px_render/...`：找到 `RpClientConnected` 的填充位置（可能在 `rd_main.cpp` 或连接管理类中）。
- `src/px_panel/src/render_panel/network/ws_panel_server.cpp`：透传，无需改动，除非需要默认值兜底。
- `web/px_cms/src/views/SecurityInternal.vue`：如新增类型，需要映射中文展示。

---

#### 3.1.2 文件传输真实 `success` / `duration` 透传

**问题**：

1. `RpFileTransferEnd` / `CpFileTransferEnd` 只带了 `success`，没有 `duration`。
2. Panel 上报 CMS 更新时也只发了 `end` 和 `success`。
3. CMS `SpvrFileTransfer` / `SpvrUpdateFileTransfer` 没有 `duration` 字段，API 消费者无法直接拿到耗时。
4. 前端目前用 `end - begin` 计算，但若 `begin` 缺失或 `end == 0`，展示会不准确。

**待做**：

1. **C++ 侧计算 duration**：
   - 在 `FileTransferRecordOperator::GetFileTransferRecordByFileId()` 获取到已有记录后，计算 `duration = end - begin`。
   - 在 `FileTransferRecord::AsUpdateJson()` 中增加 `"duration": duration_`。
   - 在 `ws_panel_server.cpp` 的 render/client end 分支里，把计算出的 duration 填入 record 再上报。

2. **Rust CMS 侧存储 duration**：
   - `rust_server/px_cms_server/src/record/spvr_file_transfer.rs` 中 `SpvrFileTransfer` 与 `SpvrUpdateFileTransfer` 都增加：
     ```rust
     #[serde(default)]
     pub duration: i64,
     ```
   - `spvr_file_transfer_manager.rs` 的 `update_file_transfer_info` 中，`$set` 增加 `duration`：
     ```rust
     set_doc.insert("duration", update.duration);
     ```

3. **前端兜底逻辑**：
   - `web/px_cms/src/views/SecurityInternal.vue` 中「传输耗时」优先使用 `scope.row.duration`，不存在时再用 `end - begin`。

4. **兼容性**：
   - 旧 MongoDB 文档没有 `duration`，`#[serde(default)]` 保证反序列化不会失败。
   - 前端 `duration?: number` 已经是可选。

**涉及文件**：

- `src/px_panel/src/render_panel/database/file_transfer_record.{h,cpp}`
- `src/px_panel/src/render_panel/database/file_transfer_record_operator.cpp`
- `src/px_panel/src/render_panel/network/ws_panel_server.cpp`
- `rust_server/px_cms_server/src/record/spvr_file_transfer.rs`
- `rust_server/px_cms_server/src/record/spvr_file_transfer_manager.rs`
- `web/px_cms/src/views/SecurityInternal.vue`

---

#### 3.1.3 客户端虚拟文件传输结束状态上报

**问题**：`src/px_panel/src/render_panel/clipboard/win/panel_cp_virtual_file.cpp` 中：

```cpp
void CpVirtualFile::RecordFileTransferEnd() {
    const auto ft_record_op = context_->GetDatabase()->GetFileTransferRecordOp();
    ft_record_op->UpdateFileTransferRecord(file_stream_->GetFileId(),
                                           (int64_t)TimeUtil::GetCurrentTimestamp(),
                                           true);   // <-- 硬编码成功
}
```

- `success` 永远为 `true`。
- 没有调用 `NotifyUpdateFileTransferRecordToCms`，CMS 端看不到这条传输的结束。

**待做**：

1. 从 `file_stream_` 或上层调用方获取真实传输结果（是否成功、是否取消、是否出错）。
2. 修改 `RecordFileTransferEnd()` 签名，允许传入 `bool success`：
   ```cpp
   void RecordFileTransferEnd(bool success);
   ```
3. 在 `RecordFileTransferEnd()` 中调用 `NotifyUpdateFileTransferRecordToCms(record)`，确保 CMS 同步更新。
4. 检查所有调用点（如 `OnTransferEnd`、异常处理路径），传入真实状态。
5. 检查 Linux / macOS 剪贴板实现（如有同类文件传输记录逻辑）是否也需要同步修改。

**涉及文件**：

- `src/px_panel/src/render_panel/clipboard/win/panel_cp_virtual_file.cpp`
- `src/px_panel/src/render_panel/clipboard/win/panel_cp_virtual_file.h`
- 同目录下其他平台的实现（如存在）

---

### 3.2 P2：启动扫描运行时验证

**目标**：确认 `WsPanelServer::ScanAndFixUnclosedRecords()` 在真实启动流程中会被执行，并能正确修复旧记录。

**验证步骤**：

1. 关闭所有 `GammaRay.exe`、`GammaRayService.exe`、`AweSunService.exe` 等可能锁定 DLL/PDB 的进程。
2. 用 Python 或 sqlite3 在本地测试数据库插入未闭合记录：
   ```python
   import os, sqlite3, time
   db = os.path.expandvars(r'%ProgramData%\px_data\px_data.db')
   conn = sqlite3.connect(db)
   c = conn.cursor()
   old = int(time.time()*1000) - 5*60*1000
   c.execute("INSERT OR REPLACE INTO visit_record ...", (...))
   c.execute("INSERT OR REPLACE INTO file_transfer_record ...", (...))
   conn.commit(); conn.close()
   ```
3. 启动 `build_official/src/px_deps/GammaRay.exe`，等待至少 30 秒（让 `GrWorkspace::Init()` 和 `WsPanelServer::Start()` 完成）。
4. 观察以下任一证据：
   - `%ProgramData%/px_logs/godesk.log` 中出现 `ScanAndFixUnclosedRecords: fixed N visit(s), M file transfer(s)`。
   - SQLite 中对应记录的 `end`、`duration`、`success` 被更新。
   - CMS 的 `c_visit` / `c_file_transfer` 中收到更新请求（可临时在 Rust handler 中增加 `tracing::info!`）。
5. 验证结束后清理测试数据，删除 `%ProgramData%/px_data/px_data.db`（如之前不存在）。

**预期风险**：

- 若真实启动仍然无法到达 `WsPanelServer::Start()`，需要检查 `GrWorkspace::Init()` 中的前置条件（如服务注册、单实例锁、授权状态）。
- `ScanAndFixUnclosedRecords` 内部调用同步 HTTP，若 CMS 不可达，每条记录会阻塞 2 秒；后续可考虑改为异步批量上报。

---

### 3.3 P3：端到端与 UI 验证

#### 3.3.1 真实访问记录链路

1. 启动 `px_cms_server`、MongoDB、Redis。
2. 启动受控端 `GammaRay.exe`（或 `GammaRayService.exe`）。
3. 使用客户端连接一次，保持 10 秒以上后断开。
4. 打开 CMS 安全审计 → 访问记录：
   - 应出现一条记录，`end` 和 `duration` 正确。
   - 关闭客户端后刷新页面，`end` 应更新，不应再出现 `1970-01-01`。

#### 3.3.2 真实文件传输链路

1. 在客户端与受控端之间进行文件传输：
   - 拖拽文件到远程窗口。
   - 使用剪贴板复制文件。
   - 如支持，测试 RTC/UDP 文件通道。
2. 观察 CMS 安全审计 → 文件传输：
   - 文件名、`direction`（In/Out）、`success`、耗时均正确。
   - 传输失败后，`success` 应为 `false`，标签显示「失败/未完成」。

#### 3.3.3 异常恢复链路

1. 开始一次远程访问或文件传输。
2. 在连接/传输过程中强制结束 `GammaRay.exe`。
3. 重新启动 `GammaRay.exe`。
4. 确认 CMS 中对应记录被自动闭合，`success` 标记为失败，`duration` 为实际已用时间。

#### 3.3.4 并发与去重

1. 使用脚本高频重复调用 insert API 同一 `conn_id`/`the_file_id`。
2. 确认 MongoDB / SQLite 中均只有一条记录。
3. 确认 total 计数正确。

---

### 3.4 P3：数据迁移与部署清单

#### 3.4.1 MongoDB 去重（生产必做）

在 `px_cms_server` 启动时会创建唯一索引。若集合里已有重复 `conn_id` / `the_file_id`，建索引会失败并被忽略，导致 upsert 仍可能产生重复。

**升级前执行**：

```javascript
use db_gr_cms_server;

// 保留每个 conn_id 最新文档
db.c_visit.aggregate([
  { $sort: { created_timestamp: -1 } },
  { $group: { _id: "$conn_id", doc: { $first: "$$ROOT" } } },
  { $replaceRoot: { newRoot: "$doc" } },
  { $out: "c_visit_dedup" }
]);
db.c_visit.drop();
db.c_visit_dedup.renameCollection("c_visit");

// 保留每个 the_file_id 最新文档
db.c_file_transfer.aggregate([
  { $sort: { created_timestamp: -1 } },
  { $group: { _id: "$the_file_id", doc: { $first: "$$ROOT" } } },
  { $replaceRoot: { newRoot: "$doc" } },
  { $out: "c_file_transfer_dedup" }
]);
db.c_file_transfer.drop();
db.c_file_transfer_dedup.renameCollection("c_file_transfer");
```

> 注意：如果集合很大，建议加索引 `{ conn_id: 1 }`、`{ the_file_id: 1 }`、`{ created_timestamp: -1 }` 后再做聚合，或在低峰期执行。

#### 3.4.2 SQLite 本地库升级

`sqlite_orm` 的 `sync_schema()` 会尝试新增唯一约束。若旧表已存在重复数据，可能抛出异常并触发数据库重建（代码里已有备份逻辑）。

**建议**：

1. 在升级说明中提醒用户：首次启动前可手动备份 `%ProgramData%/px_data/px_data.db`。
2. 如需要保留历史数据，可编写一个 Python 去重脚本在升级前运行：
   ```python
   import sqlite3
   db = r"C:\ProgramData\px_data\px_data.db"
   conn = sqlite3.connect(db)
   conn.execute("DELETE FROM visit_record WHERE id NOT IN (SELECT MAX(id) FROM visit_record GROUP BY conn_id)")
   conn.execute("DELETE FROM file_transfer_record WHERE id NOT IN (SELECT MAX(id) FROM file_transfer_record GROUP BY the_file_id)")
   conn.commit(); conn.close()
   ```
3. 若用户无重要历史数据，直接让 panel 自动重建空库也可接受。

#### 3.4.3 部署顺序

1. 备份 MongoDB `db_gr_cms_server`。
2. 运行去重脚本。
3. 部署新版 `px_cms_server`、前端 `web/px_cms/dist`。
4. 启动 `px_cms_server`，确认日志没有 "create visit conn_id index failed" 等警告。
5. 部署新版 `GammaRay.exe` / `GammaRayService.exe`。
6. 首次启动后检查本地 SQLite 是否成功升级，以及启动扫描日志。

---

### 3.5 P4：安全加固与文档

#### 3.5.1 记录接口安全

当前 `/api/v1/record/*` 仅通过 URL `appkey` 校验，存在被截获/重放的风险。后续可考虑（优先级由低到高）：

1. **时间戳 + HMAC 签名**：panel 与 CMS 共享 `app_secret`，请求带 `ts` 和 `sign`，CMS 校验时间窗口（±5 分钟）和签名。
2. **HTTPS 双向 TLS**：使用项目已有的 `certs/cert.pem`、`key.pem` 做 mTLS。
3. **短期 token**：由授权服务器签发，panel 每次上报前换取 CMS token。

#### 3.5.2 文档更新

需要补充或更新的文档：

- `docs/px_cms_server_runtime_config.md`（如不存在则新建）：运行环境、端口、MongoDB/Redis 要求。
- `docs/security_audit_next_steps.md`：本文档。
- 部署手册中增加「升级前 MongoDB/SQLite 去重」章节。
- README 中说明安全审计功能的启用条件（需要 CMS 服务在线）。

---

## 4. 测试矩阵

| 测试项 | 测试方法 | 通过标准 |
|--------|---------|---------|
| C++ 编译 | `build_official.bat incremental` | 无错误，生成 `GammaRay.exe` |
| Rust CMS 编译 | `cargo build -p px_cms_server --release` | 无错误 |
| Web 编译 | `cd web/px_cms && npm run build` | 无类型错误 |
| 访问记录 insert/update | curl | 200，DB 只有一条 |
| 文件传输 insert/update | curl | 200，DB 只有一条 |
| 过滤查询 total | curl 带 `visit_device_id` | `total` 与过滤结果一致 |
| update-before-insert | curl update 不存在的 ID | 自动创建默认文档 |
| 启动扫描 | 手动插入未闭合记录 + 启动 GammaRay | DB/CMS 被更新 |
| 真实访问链路 | 客户端连接/断开 | CMS 页面记录正确 |
| 真实文件传输链路 | 拖拽/剪贴板传文件 | CMS 页面记录正确 |
| 异常恢复 | 传输中 kill panel | 重启后记录闭合 |

---

## 5. 迁移脚本模板

### 5.1 MongoDB 去重脚本

保存为 `scripts/dedup_cms_records.js`：

```javascript
// scripts/dedup_cms_records.js
// 用法：mongo localhost:27017/db_gr_cms_server scripts/dedup_cms_records.js

function dedup(collectionName, idField) {
    var src = db.getCollection(collectionName);
    var tmpName = collectionName + "_dedup_tmp_" + Date.now();
    var tmp = db.getCollection(tmpName);
    tmp.drop();

    src.aggregate([
        { $sort: { created_timestamp: -1 } },
        { $group: { _id: "$" + idField, doc: { $first: "$$ROOT" } } },
        { $replaceRoot: { newRoot: "$doc" } },
        { $out: tmpName }
    ]);

    src.drop();
    tmp.renameCollection(collectionName);
    print("Deduped " + collectionName + " by " + idField);
}

dedup("c_visit", "conn_id");
dedup("c_file_transfer", "the_file_id");
```

### 5.2 SQLite 去重脚本

保存为 `scripts/dedup_panel_db.py`：

```python
#!/usr/bin/env python3
# scripts/dedup_panel_db.py
import os
import sqlite3
import shutil
import datetime

db_path = os.path.expandvars(r"%ProgramData%\px_data\px_data.db")
if not os.path.exists(db_path):
    print("DB not found:", db_path)
    exit(0)

backup = db_path + ".dedup." + datetime.datetime.now().strftime("%Y%m%d%H%M%S") + ".bak"
shutil.copy2(db_path, backup)
print("Backup:", backup)

conn = sqlite3.connect(db_path)
conn.execute("""
    DELETE FROM visit_record
    WHERE id NOT IN (
        SELECT MAX(id) FROM visit_record GROUP BY conn_id
    )
""")
conn.execute("""
    DELETE FROM file_transfer_record
    WHERE id NOT IN (
        SELECT MAX(id) FROM file_transfer_record GROUP BY the_file_id
    )
""")
conn.commit()
conn.close()
print("Dedup done.")
```

---

## 6. 提交与发布计划

### 6.1 提交策略

建议分 2~3 个 commit，保持清晰：

1. **commit 1 - C++ Panel 修复**：
   - SQLite 唯一约束、upsert、启动扫描。
2. **commit 2 - Rust CMS 修复**：
   - 唯一索引、upsert、过滤 total、`duration` 字段（如本次加入）。
3. **commit 3 - Web 前端修复**：
   - 文件传输表字段、搜索分页修复。

> 当前用户已要求**不要自动提交**，所有改动仍停留在工作区，等待 review 后由用户决定提交时机。

### 6.2 回滚方案

- **C++ 侧**：回退到上一版本可执行文件即可，SQLite 数据不受影响（若已升级 schema，旧版代码仍能读写）。
- **Rust CMS 侧**：回退可执行文件后，MongoDB 中的 `duration` 字段多余但无害；唯一索引已存在也不会影响旧版逻辑。
- **Web 侧**：回退 `web/px_cms/dist` 到上一个构建产物。

### 6.3 发布前 Checklist

- [ ] P2 字段透传完成并通过测试。
- [ ] 启动扫描在真实 GammaRay 启动中验证通过。
- [ ] 真实访问记录 + 文件传输链路在 CMS 页面可见且正确。
- [ ] MongoDB 生产库已去重，唯一索引创建无警告。
- [ ] 部署文档已更新。
- [ ] 全量构建 `build_official.bat` 通过（默认 `-j18`，注意内存/并发）。
- [ ] 用户确认后提交并推送。

---

## 7. 优先级速览

| 优先级 | 工作项 | 建议负责模块 |
|--------|--------|-------------|
| P2 | UDP/RTC `conn_type` 取值统一 | render + panel |
| P2 | 文件传输 `duration` 透传到 CMS | panel + Rust + web |
| P2 | 虚拟文件传输真实 `success` 与 CMS 更新 | panel 剪贴板 |
| P2 | 启动扫描真实启动验证 | panel + 手工测试 |
| P3 | 端到端访问/传输/UI 验证 | 全链路手工测试 |
| P3 | 数据迁移脚本与部署文档 | 运维/文档 |
| P4 | 记录接口签名/mTLS 加固 | Rust CMS |
| P4 | 最终打包与提交 | 构建/发布 |
