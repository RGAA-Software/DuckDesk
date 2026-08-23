# 文件传输整体替换计划：废弃现有方案，全面采用 RustDesk 方案

> 目标：**整体废弃** GammaRay 现有文件传输（`px_file_transfer.proto` 消息族 + render 插件 +
> Qt 客户端插件 core + Web `file_transfer.ts`），替换为 rustdesk 的协议语义与传输引擎
> （`FileAction`/`FileResponse` 消息族 + `fs.rs` 的 `.download`/`.digest` 续传、Digest 覆盖确认、路径安全校验）。
>
> 参考代码：`rustdesk/`（仓库根下 clone 的 rustdesk master，`7aa98d43c`）。
> 本文档取代此前的"增量嫁接"思路。

---

## 0. 总体架构决策

```
主控端(Qt)                网络承载(不动)              被控端
┌──────────────────┐   ft_data_channel/WS/Relay   ┌─────────────────────────┐
│ 新插件 ft:        │ ───────────────────────────► │ render 新插件 ft:        │
│  UI 全新          │   px::Message envelope       │   消息广播 OnMessage      │
│  core=引擎适配    │ ◄─────────────────────────── │   └─► C++ FT 引擎        │
└──────────────────┘   FileAction / FileResponse  │     (共享库,独立线程)    │
共享引擎库: src/px_deps/ 下新增 ft_engine(C++),render ft 插件与 Qt ft 插件共用
Web 主控端: 文件传输窗口全新重写
剪贴板(px_user_proxy 等)与文件传输完全无关,一行不动
```

四项关键决策：

1. **引擎落点：被控端就在 render 进程内，做成重写的 FT 插件（C++），不经过 px_user_proxy。**
   - 消息到达路径现成：net 插件 → `plugin_net_event_router.cpp` 广播 → FT 插件 `OnMessage`；回包走 `DispatchTargetFileTransferMessage`，与现有 FT 插件完全相同的出入口。
   - **磁盘 IO 必须在插件内独立工作线程**（消息队列 + worker thread），不在路由分发线程上做文件读写——`px_plugin_interface.cpp:283-294` 有 200ms 慢插件告警，分发线程阻塞会拖累整个媒体管线。这是本方案唯一新增的结构件。
   - px_user_proxy 与剪贴板体系（`render_client.rs`、`clipboard_plugin.cpp`）完全不参与、不改动。
2. **引擎做成共享 C++ 库（`src/px_deps/` 下新增，如 `px_ft_engine`），render 插件和 Qt 客户端插件共用。**
   - rustdesk 的 `fs.rs` 本来就是双端共享的一份代码；GammaRay 被控端（render）和主控端（Qt）都是 C++，共享一个引擎库正好复刻这个结构，避免两份实现漂移。
   - 引擎库只依赖 proto 生成代码和标准库/已有公共库，通过回调接口收发消息（由插件层注入发送函数），不感知网络通道。
   - 不 vendor hbb_common：它用 rust-protobuf、带 5 个 git 依赖和整套 TLS/sodium 包袱，且 `fs.rs` 硬耦合其自家 `Stream` 类型（fs.rs:18,893,1322）。但 `fs.rs`（约 1800 行）是移植蓝本：`.download` 临时文件、`.digest` 续传凭证、size+mtime 覆盖检测、路径穿越/符号链接校验（含单测）逐函数对照用 C++ 重写。
3. **协议封装：rustdesk 消息族作为 `px::Message` 新 oneof 字段，通道层零改动。**
   - `rustdesk/libs/hbb_common/protos/message.proto` 的 File 族消息（message.proto:355-503）字段简单、无外部 import，是 proto3，可直接用 prost-build / protoc / protobufjs 三端编译。
   - 挂进 `px::Message` oneof（可复用废弃的 260-330 号段），则 TLV 分片、反压、router 广播、relay 房间、ft 通道**全部不动**。
   - 剪贴板消息（`kClipboardInfo`/`kClipboardReqBuffer` 等在 `px_message.proto`，与 FT 协议解耦）继续走 ft 通道，**剪贴板文件功能天然保留**。
4. **主控端全部新作：新插件统一命名为 `ft`，UI 不保留、直接重写。**
   - Qt：新建 `src/px_client/plugins/ft/`（core = 引擎薄适配层 + 全新双栏文件管理 UI），旧 `file_transfer_client` 插件（core + widget 全套约 7600 行）整目录删除。
   - Web：文件传输窗口全新重写（`FileTransferWindow.vue`/`useFileTransfer.ts`/`file_transfer.ts` 整套替换），不迁就旧组件的 API 形状。
   - UI 交互参照 rustdesk 桌面端三栏布局（本地列表 | 远程列表 | 传输队列/进度），支持拖拽互传、覆盖确认"应用到全部"。

**新旧版本不互通**：旧端收到新 oneof 字段会走 default 丢弃。需要版本门控——在握手/SyncConfig 中声明 FT 协议版本，不匹配时 FT 功能置灰并提示，而不是静默失败。

---

## 1. 拆除清单（废弃现有方案）

**协议层**
- `src/px_deps/px_message_new/px_file_transfer.proto` — 整文件内容替换为 rustdesk File 族消息
- `src/px_deps/px_message_new/px_message.proto:5`（import 保留，文件内容变了）、`:68-80`（enum 260-330 号段清理）、`:723-736`（oneof 字段换成新消息）
- `web/px_web_client/proto/px_file_transfer.proto` 同步 + `web/px_web_client/src/rtc/proto.ts:35-60` 常量表清理

**render 被控端**
- `src/px_render/plugins/file_transfer/` **整目录删除**（plugin + file_transmission_server 约 1600 行 + toml）
- 挂接点清理：`src/px_render/plugins/CMakeLists.txt:20`、`plugin_ids.h:30`、`plugin_manager.cpp:335-337`——随后由新插件 `ft` 接替（新 ID、新目录）
- `plugin_net_event_router.cpp:315-325` 旧 case 空壳清理（广播机制本身不动）

**Qt 客户端**
- `src/px_client/plugins/file_transfer_client/` **整目录删除**（core 状态机 + widget UI 全套约 7600 行 + 资源 + toml）+ 挂接点（`plugins/CMakeLists.txt:2`、`ct_plugin_ids.h:16`、`ct_plugin_manager.cpp:211-213`）——由新插件 `ft` 接替
- `src/px_client/transfer/file_transfer.{h,cpp}` 删除（死代码，从未实例化）+ `ct_base_workspace.h:38,157` 残留声明
- 入口接线保留并指向新窗口：`MsgClientOpenFiletrans`（ct_app_message.h:177）、`ct_base_workspace.cpp:885-891`、悬浮球菜单（ct_game_view.cpp:318-323、float_controller_panel.cpp:360）

**panel / 设置（改，不是删）**
- `src/px_panel/src/render_panel/transfer/file_transfer.{h,cpp}` 已标 `@Deprecated`，删除 + `ws_panel_server.cpp` 的 `/file/transfer` 旧路由清理
- 审计链路（`ws_panel_server.cpp:633-671` kRpFileTransferBegin/End → Console）保留，事件源改由 render `ft` 插件发出（`plugin_event_router.cpp:252` 现成路径）
- `file_transfer_enabled` 开关语义保留，执行点为新 `ft` 插件入口（SyncConfig 下发链路不动）

**Web 端**
- `web/px_web_client/src/rtc/file_transfer.ts`、`useFileTransfer.ts`、`FileTransferWindow.vue` **整套删除重写**
- 测试钩子适配：`scripts/cdp_features_test.mjs`、`web/px_web_client/test/ft_cdp_test.mjs`、`ft_pytest.py`

**通道层（不动）**：`ft_data_channel` 建立（rtc_connection.cpp:280-298、rtc_server.cpp:91/95、App.vue:1031）、WS `/file/transfer` 路由、relay FT 房间、TLV 分片重组、反压水位、`sdk_net_client.cpp:459-525` 三路分发——全部保留，只是承载的消息内容变了。

---

## 2. 分阶段实施

### 阶段 1：协议移植（proto）

1. 从 `rustdesk/libs/hbb_common/protos/message.proto` 摘出 File 族消息（FileEntry/FileDirectory/FileAction/FileResponse/FileTransferSendRequest/FileTransferReceiveRequest/FileTransferBlock/FileTransferDigest/FileTransferDone/FileTransferError/FileTransferSendConfirmRequest/FileTransferCancel/FileDirCreate/FileRemoveDir/FileRemoveFile/FileRename/ReadDir/ReadAllFiles/ReadEmptyDirs 及嵌套），移植进 `px_file_transfer.proto`（整文件重写），包名改 px 体系。
2. `px_message.proto` oneof 增加 `FileAction file_action` / `FileResponse file_response` 两个字段即可（rustdesk 的两个总线消息），复用 260-330 号段。
   - **对 rustdesk 协议的增强**：`FileTransferDigest` 增加可选 `bytes file_hash` 字段（如 sha256 或分块滚动校验），第一版引擎只预留与透传、可不强校验——rustdesk 原版只靠 size+mtime 判断同一文件，存在错拼风险，hash 字段为后续强校验留口。
3. 加 FT 协议版本字段（SyncConfig 或握手消息），用于新旧不互通时的门控提示。
4. 两端代码生成对齐：C++（protoc，render 与 Qt 客户端）、TS（protobufjs）。Rust 侧 build.rs 本就全量编译 px_message_new/*.proto，无需新增配置，也不参与 FT。

验收：C++/TS 两端编译通过，proto 字段编号与 rustdesk 原版保持对照注释，便于后续对照上游。

### 阶段 2：共享 C++ FT 引擎 + render 新插件 `ft`（核心阶段）

新增共享引擎库 `src/px_deps/px_ft_engine/`（纯 C++，不感知网络通道，收发消息通过回调注入），对照 `rustdesk/libs/hbb_common/src/fs.rs` 逐函数重写：

1. **TransferJob 读写引擎**（对照 fs.rs）：
   - 读路径（下载）：`new_read` 递归展开文件列表 → 逐文件发 Digest 等确认 → `read()` 每块 128KB（对齐 `BUF_SIZE`，fs.rs:953）→ 发 FileTransferBlock
   - 写路径（上传）：`new_write` → `write(block)` 落 `<路径>.download` 临时文件 + 维护 `<路径>.digest` JSON 凭证（fs.rs:760）→ 收齐后 `modify_time()`：删 digest、rename、恢复 mtime（fs.rs:704）
   - Digest 握手：`is_write_need_confirmation`（fs.rs:1449）——优先匹配 `.digest`+`.download` 续传场景回 `transferred_size`；否则比对 mtime+size 得 identical/need_confirm
   - `confirm()` + `set_stream_offset()`：收到 offset_blk 后 seek 续传（注意 rustdesk 的 offset_blk 实为**字节偏移**）
   - 取消：remove_job，写侧清 `.download`（显式取消清除、断线保留供续传）
2. **路径安全校验**：`validate_file_name_no_traversal` / `validate_no_symlink_components` / `join_validated_path`（fs.rs:460-554）逐条移植，**连同 rustdesk 的单测用例一起移植**为 C++ 单测。
3. **块压缩**：对齐 `compress.rs` + `is_compressed_file`（fs.rs:454，xz/gz/zip/7z/rar/bz2/tgz/png/jpg 跳过），用 px_deps 已有压缩库（miniz/zlib 系），不引新依赖。
4. **作业调度**：`handle_read_jobs`（fs.rs:1336）语义——timer 驱动、每 tick 推进一个非等待作业一块。作业序列化用于进度上报。
5. **目录操作**：ReadDir（Windows 下 `/` 列盘符，fs.rs:35）、ReadAllFiles、ReadEmptyDirs、Create/RemoveDir/RemoveFile/Rename 全套。

render 新建插件 `ft`（`src/px_render/plugins/ft/`，薄壳，新插件 ID）。

> 壳为什么存在：render 的消息入口就是 router 广播给插件 `OnMessage`，插件体系同时承载生命周期、
> `file_transfer_enabled` 开关下发、审计事件上报（plugin_event_router）三项现成机制；壳还负责把磁盘 IO
> 跳离分发线程（worker 线程 + 队列）。协议与文件系统语义全部在共享引擎库里，壳不做第二份实现。

6. **插件壳**：`OnMessage` 过滤 FileAction/FileResponse → 投递到引擎工作线程的消息队列；引擎产出的 FileResponse 经 `DispatchTargetFileTransferMessage` 回包。线程模型：**一个 worker 线程 + 队列 + 定时器**，所有磁盘 IO 在 worker 上，分发线程只做入队。

   线程模型细则（正确性假设，实施时逐条落实）：
   - **单消费者串行化**：TransferJob 表、文件句柄、`.digest` 凭证等全部可变状态仅 worker 线程持有；分发线程只把解析好的 proto 消息 move 进队列，不共享引用。跨线程共享只有队列本身。
   - **写盘顺序 = 网络顺序**：ft 通道有序（ordered+reliable），worker 按到达顺序写块，复刻 rustdesk"块无序号、靠底层有序"的协议假设。Cancel/SendConfirm/Block 同队列按序处理，不存在取消后落块、确认前落块的乱序。
   - **回包线程**：阶段 2 开工先核实 `DispatchTargetFileTransferMessage` 是否可从非分发线程调用；若否，壳内加回程队列 marshal 回分发线程。
   - **关闭路径**：插件 Stop → 停收新消息 → 在途作业按取消语义处理 → join worker，保证析构时无在写文件。
   - **单 worker 是刻意的**：对齐 rustdesk 调度（每 tick 一个作业一块），磁盘 IO 本是串行瓶颈；吞吐不够时调 tick 间隔或每 tick 多块，不引入多线程写。
   - **回执与流控对齐 rustdesk 语义**：块级无 ack 无序号（rustdesk 的 `blk_id` 字段本身就闲置），可靠性靠 ft 通道有序可靠；文件级 Digest/Confirm/Done/Error 握手照搬。**差异点**：rustdesk 靠 TCP `await send` 隐式反压，我们走引擎发送回调——回调必须能返回"通道忙"（水位满），忙时该 tick 不读盘、不积块，防止壳层队列无限堆积。
   - **限速钩子（相对 rustdesk 的增强）**：rustdesk 无带宽限制，而旧协议有 `max_transmit_speed` 设置项、不能回退——引擎发送回调处留令牌桶限速钩子（默认不限速），设置值沿用现有 SyncConfig 下发链路。
   - **速度显示**：照搬 rustdesk 的 1s 差值法（`update_jobs_status`，io_loop.rs:1048）：每秒 `transferred` 差值 ÷ 间隔得速度，随进度回调给 UI，不影响传输本身。
7. **权限与审计**：
   - `file_transfer_enabled` 关闭时插件入口直接拒绝并回错误（rustdesk 回 "No permission of file transfer"）
   - 逐操作审计（列目录/上传/下载/删除/重命名/建目录）上报 kRpFileTransferBegin/End 等事件，对接 panel 现有 Console 记录链路（`plugin_event_router.cpp:252` 现成路径）
   - 文件数上限（对齐 `check_file_count_limit`）入设置

验收：引擎库单测全绿（含移植的 rustdesk 安全用例）；用脚本经 WS `/file/transfer` 路由直接发 FileAction，完成列目录/上传/下载/续传/取消全流程；sha256 一致性校验；大文件传输期间媒体画面不卡（验证 worker 线程隔离）。

### 阶段 3：Qt 主控端新插件 `ft`（core + 全新 UI）

新建 `src/px_client/plugins/ft/`，替代整个旧 `file_transfer_client`：

1. **core = 共享引擎库（阶段 2 的 `px_ft_engine`）的薄适配层**，协议状态机全部来自引擎，插件只负责：
   - 消息收发接线（引擎回调 → `PostFileTransferMessage`；收到 FileResponse → 喂给引擎）
   - 主控侧语义（对照 `rustdesk/src/client/io_loop.rs:565-1069, 1515-1779`）：read_jobs/write_jobs 双队列；SendFiles（上传=本地 new_read + 发 receive；下载=发 send + 本地 new_write）
   - Digest 收到后：上传方向查 `default_overwrite_strategy` 或回调 UI 弹确认框；下载方向本地决策 identical/skip/need_confirm
2. **全新 UI**（Qt Widgets，参照 rustdesk 桌面端三栏布局）：
   - 本地文件列表 | 远程文件列表 | 传输队列（每作业进度条/速度/状态）
   - 拖拽互传（本地↔远程面板互拖 + 从操作系统拖入）
   - 覆盖确认弹框：跳过/覆盖/续传 + "应用到全部冲突"勾选（`default_overwrite_strategy`）
   - 目录工具：新建文件夹/重命名/删除（带递归确认）/刷新
3. 作业排队（`is_last_job` 挂起语义，io_loop.rs:673）与每秒速度计算（io_loop.rs:1024-1069）。
4. 空目录流程：目录上传前先 ReadEmptyDirs + 批量 FileDirCreate（对照 flutter file_model.dart:570）。
5. 入口接线：`MsgClientOpenFiletrans` 打开新窗口；退出时若有进行中作业提示（沿用 `ct_base_workspace.cpp:763` 的 HasProcessingTasks 检查点，改对接新插件）。

验收：Qt 客户端对阶段 2 的被控端完成全部操作；覆盖确认三选项 + "应用到全部"正确；目录含空目录上传完整。

### 阶段 4：Web 主控端全新重写

1. `web/px_web_client/src/rtc/file_transfer.ts` + `useFileTransfer.ts` + `FileTransferWindow.vue` **整套新作**：协议层实现同一套 rustdesk 语义（protobufjs 解出新 oneof 字段，TLV/反压水位保留），UI 与 Qt 版同款三栏布局与交互。
2. 目录递归（上传/下载文件夹）按 ReadEmptyDirs/FileDirCreate/ReadAllFiles 语义实现。
3. **续传降级**：浏览器无法写 `.download` 旁挂文件——Web 侧下载支持内存中记录已收量做会话内续传，刷新页面则整文件重传；上传侧正常支持（续传凭证在被控端引擎一侧，与主控形态无关）。
4. 测试脚本适配（`ft_cdp_test.mjs` 等）。

### 阶段 5：拆除与收尾

1. 按 §1 清单删除旧代码：render `file_transfer` 插件整目录、Qt `file_transfer_client` 整目录、`src/px_client/transfer/`、panel transfer 旧路由、proto 旧字段、Web 旧文件传输三件套。
2. 新旧版本门控：握手协商 FT 协议版本，不匹配时入口置灰 + 提示。
3. 文档更新：`docs/clipboard_file_transfer.md` 补充通道说明（剪贴板仍走 ft 通道，消息不变）；AGENTS.md/README 中相关描述更新。

---

## 3. 测试计划

**测试环境**：实机被控端 `10.0.0.90`（账号 `administrator`，密码 `dolit@321`，仅用于内部联调）。部署最新 render 到该机，主控端从开发机（Qt）和浏览器（Web）分别连入测试。部署可复用 `tests/_deploy_*_70.bat` 系列脚本的模式新增 ft 部署脚本。

**双机联调执行方式**：
- 部署：`net use \\10.0.0.90`（administrator 凭据）→ 拷贝 render 产物 → 远程重启服务；Console/relay 用现有测试环境。
- 被控机预置数据集（一次性脚本）：含空目录/嵌套/特殊字符的目录树；1GB 随机文件（附 sha256）；1GB 全零文件（压缩用例）；zip/jpg 批（跳过压缩用例）；同名同 mtime 不同内容的冲突陷阱文件。
- 驱动：阶段 2 用 Python 脚本直连 render WS `/file/transfer` 发 FileAction（引擎独立验收）；Web 端用 CDP 自动化（改写 `ft_cdp_test.mjs`）；Qt 端手动操作 + 结果脚本化断言。
- 断言：源/目标 sha256 比对、`.download`/`.digest` 残留检查、Console 审计记录、render 日志（慢插件告警）。
- 断线用例：1GB 传至 50% 时 taskkill render 或断网 → 恢复 → 重连续传 → hash 比对。

**单元测试（引擎库 `px_ft_engine`，CI 可跑）**

| 测试 | 内容 |
|---|---|
| 路径安全 | 移植 rustdesk fs.rs 全部校验单测：`..`、`C:\abs`、`\\unc`、symlink 组件、空字节全拒绝 |
| 读写引擎 | 模拟消息对（loopback 注入 Block/Confirm）：写盘→读盘往返 hash 一致；`.digest` 凭证生成/消费/清理 |
| 续传 | 写到 50% 中断 → 重新 Digest → offset 续传 → 最终 sha256 一致；`.download` 不残留正式文件名 |
| 覆盖决策 | identical→skip、mtime/size 不同→need_confirm、NoSuchFile→offset 0 三分支 |
| 压缩 | 文本压缩率、已压缩后缀跳过、解压往返一致 |
| 调度 | 多作业排队推进、等待作业不占 tick、取消清理 |

**集成/E2E 测试（对 10.0.0.90 实机）**

| 测试 | 内容 |
|---|---|
| 全操作链路 | 列目录（含 `/` 盘符）/上传/下载/新建目录/重命名/删除/取消，Qt 与 Web 双主控各一遍 |
| 大文件续传 | 1GB 文件传到 50% 断网/杀连接 → 重连续传 → sha256 源/目标一致 |
| 覆盖确认 UI | 同名同内容→skip；同名不同内容→弹框三选项；"应用到全部"批量验证 |
| 目录完整性 | 含空目录、嵌套、特殊字符名的目录树上传/下载后逐文件比对 |
| 队列/取消 | 多目录拖入排队；取消排队作业不影响进行中；取消清理 `.download` |
| 断线恢复 | RTC 断线 data channel 重建后作业状态对齐续传 |
| 通道矩阵 | RTC P2P / RTC 局域网 / WS / Relay 四承载各跑大文件 |
| 隔离性 | 大文件传输期间远控画面流畅（worker 线程隔离验证），无 200ms 慢插件告警 |
| 限速 | 设置 max_transmit_speed 后实测吞吐被限制在阈值附近；默认不限速不回归 |
| Digest hash | hash 字段透传/预留可用；构造同 size+mtime 不同内容的文件验证不依赖 hash 时的回退行为 |
| 剪贴板回归 | 替换后剪贴板文本/文件双向粘贴不受影响（共用 ft 通道的回归验证） |
| 审计 | 10.0.0.90 上各操作后 Console 传输记录完整（事件源切换到 ft 插件后不断档） |
| 版本门控 | 旧版主控连新被控/反向组合，FT 置灰提示而非静默失败 |

---

## 4. 工作量估算与顺序

| 阶段 | 内容 | 预估 | 依赖 |
|---|---|---|---|
| 1 | proto 移植 + 双端生成 | 1–2 天 | — |
| 2 | `px_ft_engine` 共享引擎 + render 新插件 `ft` | 5–7 天（核心，对照 fs.rs 逐函数） | 1 |
| 3 | Qt 新插件 `ft`（引擎适配 + 全新 UI） | 4–6 天 | 1、2 |
| 4 | Web 文件传输整套新作 | 3–4 天 | 1、2（可与 3 并行） |
| 5 | 拆除旧代码 + 版本门控 + 文档 | 1–2 天 | 3、4 |

顺序：1 → 2（引擎先行，可独立脚本验收）→ 3 与 4 并行 → 5。测试贯穿：阶段 2 交付引擎单测，阶段 3/4 各阶段实机（10.0.0.90）回归。

---

## 5. 风险与坑（源码实证）

1. `offset_blk` 名义是块号实为字节偏移——移植时直接命名 `offset_bytes` 避免继承误导（若保持与 rustdesk 协议逐字段兼容则保留原名并在注释中说明）。
2. 写文件必须先落 `.download` 再 rename，否则中断留半个正式文件。
3. rustdesk 块级无序号无 ack，靠底层有序——ft_data_channel 是 ordered+reliable，WS/Relay 也是有序流，满足前提；但 RTC 断线重建 data channel 后作业状态要能对齐（靠 `.digest` 续传链路兜底）。
4. 块大小不匹配：rustdesk 块 128KB，render 发送侧 TLV 分片阈值也是 128KB——128KB 块 + proto 开销可能恰好在分片边界，建议块载荷定为略小于阈值（如 120KB）避免每块都触发分片。
5. Windows 路径分隔符双向转换（`transform_windows_path`）；Qt/Web 侧路径表示统一为 `/`。
6. rustdesk 每 tick 单作业单块（fs.rs:1376 `break`），吞吐上限约 128KB/1ms tick——理论足够，若实测不够可缩短 tick 或多块/tick，但不要在第一版改调度语义。
7. **引擎线程隔离必须做实**：所有磁盘 IO 在插件 worker 线程，分发线程只入队——否则触发 `px_plugin_interface.cpp:283-294` 的慢插件告警并可能拖累媒体管线（旧实现就曾在这条管线上假死过）。阶段 2 验收必须包含"大文件传输时画面不卡"。
8. Web 端续传只能会话内降级，文档与 UI 提示要写清。

## 6. 关键源码索引

**rustdesk（移植蓝本）**
- `rustdesk/libs/hbb_common/protos/message.proto:355-503` — File 族消息
- `rustdesk/libs/hbb_common/src/fs.rs` — 双端传输引擎（核心蓝本，约 1800 行）
- `rustdesk/libs/hbb_common/src/compress.rs` — 块压缩
- `rustdesk/src/client/io_loop.rs:565-1069,1515-1779` — 主控端状态机
- `rustdesk/src/server/connection.rs:3403-3696` — 被控端消息分发（CM/IPC 部分不移植）
- `rustdesk/flutter/lib/models/file_model.dart:570` — 空目录/确认流程参考

**GammaRay（改造点）**
- `src/px_deps/px_message_new/px_file_transfer.proto` / `px_message.proto:5,68-80,723-736`
- `src/px_deps/px_ft_engine/`（新增）— 共享 C++ 传输引擎，对照 fs.rs 重写
- `src/px_render/plugins/ft/`（新增，替代 `plugins/file_transfer/`）— render 插件壳：广播 OnMessage 接入 + worker 线程 + DispatchTargetFileTransferMessage 回包
- `src/px_client/plugins/ft/`（新增，替代 `plugins/file_transfer_client/`）— Qt 插件：引擎薄适配 + 全新三栏 UI
- `web/px_web_client/`（`rtc/file_transfer.ts` + `useFileTransfer.ts` + `FileTransferWindow.vue` 整套新作）— Web 重写目标
- 测试环境：被控实机 `10.0.0.90`（administrator / dolit@321，仅内部联调）

---

## 7. 实施状态（as-built，2026-08-17 完成）

全部阶段已实施并通过验证。commit 序列：

| Commit | 内容 |
|---|---|
| `575a3a7f4` | 旧 FT 整体删除（起始点） |
| `b8efb5370` | 阶段 1：proto 移植 |
| `0b7f98621` | 阶段 2：`px_ft_engine` + render `ft` 插件 |
| `7ca2345e2` | 修复：上传冲突回发 Digest、`digest.is_resume()` 续传判定 |
| `8d5f9e870` / `12b19a282` | 阶段 3：Qt `ft_client` 插件 + 全新三栏 UI |
| `6ed38b631` | 阶段 4：Web 整套新作 |
| `5030f15ec` / `42c713f4b` | 阶段 5：版本门控 + 文档 |
| `c0621a420` | 实机 E2E 发现的 P1/P2 修复 |
| `d3f61677d` | E2E 测试脚本 |

### 与计划的有意偏离（实施中定稿）

1. 压缩算法 zstd → **miniz(zlib deflate)**（仓库已有依赖，Web 端用 fflate 对齐 zlib 格式）；解压保留 256MB 炸弹上限。
2. 块载荷 **120KB**（避开 TLV 128KB 分片边界，计划 §5.4 已预见）。
3. `DispatchTargetFileTransferMessage` 经核实**线程安全**（net 插件内部自带跨线程 PostTask），worker 直调，无回程队列；"通道忙"判定用 net 插件排队水位（>256 或 buffer 不足）。
4. `InitDataStream` 先置 waiting 再发 digest（send 回调同步投递会重入，顺序与 rustdesk 相反）。
5. `ModifyTime()` 先关流再 rename（Windows 句柄语义）；且 **rename 成功才删 .digest，失败抛错并通知对端**（P2 修复，有意偏离 fs.rs 的静默 `.ok()`）。
6. `DisconnectCleanup(conn_id)` 按连接清理（E2E 发现 RTC 断线事件缺失已补：net_rtc/net_rtc_local 四个检测点派发，stream_id 用真实访客 id）。
7. 覆盖确认：被控侧 NeedConfirm 回发 `is_upload=true` Digest 由主控 UI 决策（对齐 ui_cm_interface.rs:1116）；render 无 UI 的自动跳过只作兜底。

### 验证结果

- 单测：48 个 GTest 全绿（fs.rs 路径安全用例全量移植、续传、覆盖决策、压缩、断线清理、rename 失败保凭证）。
- 构建：C++(ninja)/Web(vite)/Rust(cargo check)三端通过。
- 实机 10.0.0.90 E2E：旧被控门控置灰 ✓；新版冒烟 8/8 ✓；扩展 24/0 ✓（50MB 断点续传 offset 精确、sha256 一致；覆盖确认；空目录；特殊字符）；杀页面后作业同秒级清理 ✓。

### Qt UI 迭代(2026-08-17 实机回归驱动,第二轮)

UI 在实机回归后整体翻新为卡片式三栏结构,主要改动与踩坑:

**结构(对齐参考 UI 的交互结构,浅色主题)**
- 面包屑导航替代路径输入框:`此电脑 > D: > ...`,段按钮点击直接跳转。
- 文件表 4 列(名称/大小/修改时间/类型),表头点击排序带箭头,目录/盘符恒排前。
- 底部传输队列默认折叠为摘要条(最近任务:扩展名图标/名称/大小/方向·状态),箭头展开后占满整个窗口;不可拖动分割。
- 窗口灰底 + 左右文件栏/底部条白色圆角卡片;上传/下载为箭头按钮(朝向=数据流向),贴中间缝。
- 进度条 6px 圆角细条;取消为圆形小图标钮。

**根视图"此电脑"(协议有扩展)**
- `FileEntry` 新增可选字段 `abs_path = 6`(附加字段,旧端忽略,不破坏线兼容):根视图的盘符与常用文件夹项携带,导航以它为准,`name` 仅作显示名。
- 本地根视图:盘符(Shell 卷标显示名)+ 常用文件夹(用户目录/桌面/下载/文档/图片/音乐/视频),`SHGetFileInfoW(SHGFI_DISPLAYNAME)` 取系统本地化名称。
- 远程根视图:引擎 `ReadDir("/")` 同样追加常用文件夹。**被控端跑在 SYSTEM 服务进程**,改用活动会话用户 token 解析(`WTSGetActiveConsoleSessionId` + `WTSQueryUserToken` + 带 token 的 `SHGetKnownFolderPath` / `GetUserProfileDirectoryW`),拿到登录用户的目录;无活动会话退回进程账户。
- 盘符显示名带卷标("NewDisk (D:)"),`abs_path` 为 "D:/"。

**引擎修复**
- `ReadDir` 盘符归一化:`"D:"` 在 Windows 是"该盘当前目录"的相对语义(会解析到被控进程 CWD,实测列出了 dist 目录),统一归一为 `"D:/"` 盘根。

**踩坑记录(后续插件开发注意)**
- `px_qt_widget` 是静态库,翻译单例 `TcTranslatorManager` 在每个 dll 内是独立副本:插件 `OnCreate` 必须自行 `tcTrMgr()->InitLanguage()`,否则全部文案显示 "not find"。
- 裸 `QWidget` 子类上样式表背景(圆角卡片)不自绘,必须 `setAttribute(Qt::WA_StyledBackground, true)`。
- 语言 JSON 由 px_panel 链接后的 POST_BUILD 拷入构建目录:只改语言包不重链 px_panel 时,dist 里的 JSON 不会刷新。
- 图标:`scripts/download_material_icons.py` 从 Google Fonts(Material Symbols CDN)下载 SVG 并烘焙填充色(Qt 仅支持 SVG Tiny 子集,不支持 currentColor);ft 插件自带 `ft_res.qrc`(prefix `/ft`,避免与 panel 的 `:/icons` 资源路径冲突)。
- 构建/打包前需停掉从 dist 运行的进程与 `px_service` 服务(SCM 会自动拉起),否则 collect_dist 删 dist 目录报 Access Denied。

**验证**:引擎单测 `test_ft_path_security` 20/20、`test_ft_engine` 15/15 全绿(新增盘符归一化、根视图常用文件夹用例);实机 Qt UI 多轮回归。

### 遗留事项

- Qt 客户端真实 UI 手工回归未做（E2E 走 Web 协议级）。
- 通道矩阵只实测了 RTC；WS/Relay/RTC P2P 大文件未实测。限速与 Console 审计落库未实机核对。
- 文件数上限为常量 10000（代码内 TODO，待设置体系加字段）。
- Digest `file_hash` 预留未启用。
- 独立"仅文件传输"会话模式按计划另立项，未做。
- Qt UI 小遗留：上传冲突弹框无远端文件大小（需扩引擎回调签名）；目录操作失败无 toast；下载每条目多发一次 ReadEmptyDirs 空查询。
- RTC connected 事件 stream id 仍是 MD5（断开侧已对齐真实 id，配对如需再处理）。
