# RDP 应用模式开发计划

> 日期：2026-09-08。状态：已取得产品客户端经 WebSocket/proxy 到 90 标准用户的桌面，完成初步输入和会话保留重连；完整功能与故障验收仍在进行，见 [实施进度](rdp_implementation_progress_20260908.md)。
> 用户确认单工作区单客户端设计。本文是后续实施入口；产品决策见
> [RDP 模式设计第 0 节](rdp_application_mode_design.md#0-最新决策rdp-原生代理与会话保留)，
> 已有实现见 [自有 Qt demo 功能盘点](rdp_qt_client_reuse_inventory.md)。

## 1. 固定边界与首版范围

- 新增 `rdp` 模式，与 game-hook、webview 平行；原有模式的用户、媒体与退出行为不变。
- 工作区按 `(rdp_app_id, node_id)` 唯一映射 Windows 账号，不按访问者拆分；允许未登录访问并自动准入，无二次确认。
- px_console 生成并加密保存账号密码；Service 幂等创建和维护标准用户/SID；首版凭证自动下发到获准客户端的连接内存。
- RDP 二进制数据复用现有 WebSocket 连接和消息封装，不新增传输选型或独立 WS 连接要求。
- Windows RDP 原生图形编码在客户端解码/合成/显示；Render 不进行桌面采集，也不解码后重新编码视频。
- 每个 RDP 工作区同时只允许一个活跃客户端，忙时拒绝第二个连接，不自动抢占，不做共享观看或 shadow。
- 唯一客户端退出后，Render 沿用既有断连宽限/超时退出语义；宽限内重连取消退出，不要求常驻连接。
- Render 退出可以关闭 RDP 和代理；普通停止绝不注销 Windows 会话、删除账号/profile 或结束会话内应用。
- 下次访问按需启动 Render，核对身份并重新连接原有 Windows 会话；会话已不存在时如实反馈。
- 首版完整闭环面向 Windows GammaRay Client、本机 Console/客户端、`10.0.0.90` Windows Server 远端。
  使用原有 90，RDP 只使用非 Administrator 测试账号；不接管管理员桌面。环境变化前后均记录状态，不擅自重启或注销会话。
- 首版包括连接/认证、单连接准入、图形/光标/键鼠、resize、系统音频、文本/富格式/文件剪贴板与恢复。
  文件管理窗口、麦克风、打印机/智能卡等按后续阶段逐项接通，不能以“RDP 支持”冒充产品已支持。
- Web Console 的调度 UI 属于首版；Web 浏览器远程画面、Android/iOS/macOS RDP 客户端另行适配，
  在能力检查处明确不可用，不自动回退桌面采集/重编码。已有这些平台的其他模式不受影响。
- 不顺带重做公网传输、RAIL、USB/手柄、录像或多用户协作；不承诺未经测量的 60fps。

## 2. 已选代理边界与剩余集成验证

### 2.1 已选实现路线

使用已在 90 验证的 FreeRDP proxy 和 Windows 原生 SSPI。console 自动提供所需凭证，无交互认证步骤；
客户端仅在连接内存持有 Windows 凭证，不以“仅服务端可知 Windows 密码”为首版要求。
接下来接通现有 WebSocket/RDP 适配，再接入 console 工作区调度、Service 账号与独占管理、Render 超时退出。

```text
Console：应用+节点工作区 / 凭证权威存储 / 登录或匿名自动准入
               ↓ 调度
Service：标准账号配置与 SID 核对 / 节点独占 / Render 生命周期
               ↓
Windows Client（复用 demo） ↔ 现有 WebSocket 的 RDP 二进制消息 ↔ Render RDP 代理 ↔ 本机 Windows RDS
       解码、合成、显示                                  原生图形与通道转发
```

客户端不增加传输切换设置。Render 首版监督独立 proxy 子进程，只转发原生协议，不解码后重编码。
TCP 原型与后续 WebSocket 产品承载是验证阶段的差异，不是同时交付的两套可选传输模式。

### 2.2 已核实的复用依据

- 自有 demo：`D:/dolit/rdp`，提交 `b9183a9`；FreeRDP 实际提交
  `6c867799c4a50b7c3abf88d32c0f4e7123ce9caa`，包含 demo 自有 MF AVC444 修改；这不是已经通过产品验收的修复。
- 产品依赖已迁到官方 FreeRDP 3.31.0，固定 `aa8650b300aa4cabd85d9c72b431301509b9043f`，
  原 demo 保持只读。旧 fork 与新版本 OpenH264 实测均有 AVC444 解码失败；当前图形验收仍未完成，详见最新实施检查点。
- demo 的 `RdpSession`、`RdpView`、输入/剪贴板实现可以复用；不另写一套 RDP 解码器。
- 本地 FreeRDP `server/proxy/pf_utils.c::pf_utils_is_passthrough` 当前返回透传模式；
  `pf_server.c` 包含 `FreeRDP_DeactivateClientDecoding` 设置，`pf_update.c` 转发图形更新，
  `pf_client.c`/`pf_channel.c`/`pf_config.c` 包含通道映射、过滤和目标配置。
  已实测 AVC444v2 桌面和重连，详见 [proxy 实测记录](rdp_proxy_validation_20260908.md)；未验收通道不视为通过。
- demo 自编译配置关闭 `WITH_SERVER`，不能直接把原构建产物当作已经包含代理能力。
  本地 `server/proxy/CMakeLists.txt` 的目标为 `freerdp-server-proxy`；构建依赖需单独核对。
- FreeRDP `include/freerdp/transport_io.h` 有 IO 回调/传输层扩展入口；优先用受控适配器，
  不预设必须修改第三方内部。对照原型可用仅本机可达且有访问保护的端口桥，不能裸露一个无认证 RDP 入口。
- 当前 GammaRay Client 为 Qt 6/C++23，demo 为 Qt 5.15/C++17：Qt 事件、OpenGL、DPI 和构建适配是必做工作，
  不能把两个 Qt 主版本直接装入同一客户端进程。FreeRDP/WinPR/OpenSSL/运行库兼容性也须检查。

### 2.3 P0 必须解决的安全与传输门禁

1. 两端身份：外层 GammaRay 票据绑定访问连接（可匿名）、工作区、节点、实例代次；前后端均自动完成原生 SSPI 登录。
   固定后端目标地址、标准账号和证书身份，不能接受客户端任意填写用户名/地址后成为通用跳板。
2. 已确认凭证分工：console 生成/加密保存并自动交付，客户端按需在内存使用。临时内存不等于不能提取密码。
   通过标准用户权限、固定目标、限制裸 RDP/proxy 入口以及票据绑定限定访问范围；生产接口不照搬 demo 环境变量注入。
3. 协议代理也不能默认安全：验证前端准入、后端 NLA/TLS、证书校验、固定目标身份、拒绝认证降级、
   通道白名单和撤销能力。原型不通过则报告具体限制，不擅自禁用安全校验或改为视频重编码。
4. 外层确定复用现有 WebSocket 连接和消息封装，增加 RDP 二进制消息类型；不另起一条 WS 或另选传输协议。
   TCP 可靠有序不需重新论证。实现包长限制、分块、双向背压、关闭与代次，防止适配层漏字节、无界排队或旧数据污染新连接。
   不另做重复网络重传，不套用丢过期帧的视频队列；共享连接给控制消息预留调度机会，但不承诺消除 TCP 队头阻塞。
5. RDP 专用可靠承载是新模式的协议扩展，不是恢复 Native 普通视频的 WS 回退、KCP、RTC 或 Relay。
   既有普通 Native UDP+FEC 路线不变，公网 P2P/中继仍按独立计划后续处理。
6. 协议字节隧道基线覆盖 RDP 主可靠连接；不顺带承诺 RDP UDP 多传输、所有增强重定向、所有图形编码可用。
   实际协商结果和吞吐须记录，不能据“使用 RDP”就认为所有微软增强能力自动生效。

## 3. 实际代码落点

表中带“新增”的名称是建议落点，不是已经存在的模块。保持明确职责，不创建全局服务定位器或通用插件总线。

| 模块 | 已有落点 | 计划工作 |
|---|---|---|
| Console 应用/调度 | `rust_server/px_console_server/src/app_schedule/manager.rs` | 新增 Rdp 类型、节点能力、持久工作区引用、启动幂等、忙状态、版本检查 |
| Web Console | `web/px_console/src/entity/app_schedule.ts`、`views/AppsView.vue` | RDP 应用配置、启动/进入/断开、工作区与连接状态分别显示；不显示 Windows 秘密 |
| Console-Service 协议 | `src/px_deps/px_server_protocol/console_service.proto` | 增加类型明确的 RDP 配置/状态与能力；旧字段编号不复用，按项目流程生成 Rust/C++ 产物 |
| Service 实例 | `rust_client/px_service/service_core/src/app_instance.rs` | RDP launch spec、模式校验、瞬态实例与持久工作区分离 |
| Service 进程生命周期 | `rust_client/px_service/src/service_host.rs` | 启停/故障回收只覆盖受管 Render/代理；排除已有游戏路径扫杀逻辑 |
| Windows 账号与会话 | Service 下新增 `rdp_workspace`/Windows 适配模块 | 执行 console 账号/凭证版本、标准用户、SID、Session 核查、节点固定、启动失败恢复；不把 logoff 放入析构 |
| Render 装配 | `src/px_render/rd_app.cpp`、`settings/rd_settings.*` | 模式分派、代理启动/停止、Ready 状态、沿用断连宽限；不启动采集/编码/RTMP 分支 |
| Render 代理 | 新增 `src/px_render/rdp/` | 固定目标、与 Service 受控 IPC、协议适配、每实例连接所有权 |
| Render 网络 | `network/ws/ws_server.*`、`ws_transport.*`、`network/transport_types.h` | 新 RDP 通道与单连接路由，不用 Broadcast/SubmitEncodedVideo 承载 RDP |
| SDK | `src/px_client_sdk/connection/`、`sdk_connection_params.h` | 类型明确的 RDP 通道能力、事件/字节缓冲和取消；不把 Qt/FreeRDP GUI 类型注入平台无关核心 |
| Windows Client | `src/px_client/ct_workspace.*`、`ct_base_workspace.*`；新增内部 RDP 模块 | 按模式选择 RDP 工作区，迁入 demo 协议和 Qt 6 显示/输入；不强制先建立 UDP 视频或普通解码器 |
| 内置功能 | `src/px_client/modules/clipboard/`、`modules/file_transfer/` | RDP 能力路由与用户授权，避免与普通模式重复处理；不恢复 clipboard/ft/record DLL 插件 |
| 构建与交付 | Client/Render CMake、`scripts_build/build_cpp_*.bat` | 固定依赖和新增聚焦测试目标；Client 运行产物同步 dist 并核对哈希 |

Console 对 RDP 按 app_id/node 复用持久工作区，不将 user_id 纳入唯一键；不同访问者先后共享同一工作区是明确产品行为。
隔离验收按不同应用/节点工作区进行。同一工作区不提供访问者之间的文件/profile 隔离，不能在 UI 或文档中误称个人桌面。
Service 内存中的 `AppInstanceRegistry` 不能作为账号/profile 的唯一持久记录；也不能以 `instance_id` 的重建推导出应删除 Windows 用户。

## 4. 数据与生命周期设计

### 4.1 三类身份分开

- `workspace_id`：持久身份，唯一对应应用配置+节点和 Windows SID/账号引用；不绑定访问者，未授权不得改绑。
- `runtime_instance_id + generation`：本次 Render/代理进程，记录 PID 及创建身份、端口；可随退出释放重建。
- `connection_id + lease_generation`：当前唯一客户端占用，绑定票据、状态、撤销与过期任务；多条辅助通道共用这一占用。

匿名访问没有 user_id，但必须有独立连接身份及不可猜测的恢复凭据；不能用空 user_id 识别“同一访问者”。
console 按应用既有发布/访问策略自动准入，无强制登录或二次授权弹窗；应用不可访问时直接返回原因，不自动放行。
凭证权威记录在 console，加密密钥与数据库配套备份；Service 只持有执行所需受保护材料及已应用版本。
密码轮换按版本确认推进，失败保留可重试状态，禁止 console 与 Windows 账号悄悄使用不同版本。

Session ID 是本机易失状态，不是授权凭证；节点重启后重新发现并核对身份，不能凭旧数字直接接管。
首版工作区固定到拥有该账号/profile 的节点；节点不可用就明确失败，不自动调度到另一台机器伪装成原桌面恢复。
首次创建账号/保存秘密/首次登录必须幂等，部分失败留下可识别状态；不要为“重试干净”自动删除已存在账号或 profile。

### 4.2 独占准入与退出

由 Service 在目标节点对工作区做权威独占保留，Console 负责授权/展示，Render 在接入时核对代次和占用。
同一节点多个 Render 不能各自加一个本地锁后都声称独占成功；跨服务重启须核对活进程，防止双实例和永久假忙。

```text
未连接 → 已预留/启动中 → 已连接 → 断连宽限 → Render 已退出
                             ↑        │              │
                             └─ 重连 ─┘              └─ 下次授权启动并连接
Windows 账号/登录会话：普通断连和 Render 退出均保留；不跟随上面的临时状态删除
```

预留有启动截止时间；首次启动还没有连接也必须可取消/回收运行时，不能无限占用。
推荐宽限期内保留同一访问者的恢复权，第二个访问者仍收到忙状态；到期并确认旧运行时退出后释放。
任何旧连接回调都只能改变自己的 generation，不能释放新占用或关闭重连后的 Render。
正常停止与异常断线区分，不能在用户主动退出后继续运行 demo 原来的自动重连循环。
UI 的“断开/停止连接”与“注销 Windows/销毁工作区”明确区分；首版不新增默认注销/删除入口。

## 5. 分阶段实施与完成门禁

按依赖顺序完成，每阶段独立可编译、可审查、可回退。P0 的结果决定 P1/P2 的具体代理装配，不提前承诺全部周期。

| 阶段 | 开发内容 | 完成门禁 |
|---|---|---|
| P0 路线验证与适配 | 原生 SSPI/proxy 桌面与重连已验证；补现有 WebSocket 的 RDP 字节流适配 | 经现有 WS 收到真实桌面；有界队列/关闭正确；退出不注销；不把已知 TCP 可靠性另立研究项目 |
| P1 demo 产品内复用 | Qt 6 适配，协议与 GUI 职责分离，内部 RDP 工作区；智能所有权、可取消初始化、停止清理；移除硬编码凭据 | Client 内可连接/显示/输入/resize，普通模式不创建 FreeRDP；关闭及重复启停无残留/晚回调 |
| P2 端到端模式接入 | app type/proto/能力、受权 RDP 数据通道、Render 模式装配、两层 Ready（代理可接入、远端首帧） | 从 Console 进入 Windows Client，经 90 Render 接 RDP；不走捕获/编码/普通视频 UDP 建连门禁；旧端明确不支持 |
| P3 工作区与准入 | console 秘密权威、应用+节点映射、Service 标准账号、匿名自动准入、独占与幂等恢复 | 不同工作区隔离；同应用/节点不同访问者复用账号；双请求只一个成功；匿名不绕过票据/忙状态 |
| P4 停止与快速恢复 | 既有宽限退出、停止只回收 Render/代理、RDS 原会话复用、连接代次与权限撤销 | 宽限内重连不被旧回调杀死；超时后 Render 消失而账号/Session/应用保留；新 Render 接回原状态 |
| P5 基础桌面通道 | 系统音频、文本/HTML/DIB/文件目录剪贴板、分辨率与常用输入体验；政策与能力路由 | 双向内容与文件校验、取消/重名、可听音频、DPI/光标正确；不访问错误用户或宿主资源 |
| P6 企业辅助功能 | 独立文件管理能力选型、麦克风、受限目录/打印机/读卡器，多屏和触控真机验收 | 每项有支持范围与实测证据；无设备则明确未验收，不能把通道 ready 当作功能通过 |
| P7 性能/安全/交付 | 1080p 动态内容、AVC444 画质、两工作区并行；故障/篡改/队列测试；部署/能力文档与产物同步 | 无二次编码，无宿主数据泄漏，性能有真实统计；既有模式回归通过，dist 哈希一致 |

P0 是原型而非可发布企业功能；P0–P4 构成可进入、独占使用、退出与恢复的最小闭环。
P5 完成基本桌面使用体验，P6 逐项扩充，不因外围硬件暂缺阻塞已验收基础功能，但发布说明必须区分。
FreeRDP proxy 若无法满足必要能力，只继续安全的诊断/最小接口验证，不扩展为长期无边界的协议重写。

## 6. 功能和安全验收清单

| 领域 | 必测项目 |
|---|---|
| 连接与认证 | 初次/重连、证书可信与变更拒绝、错误账号、过期/重放票据、伪造工作区/节点、撤销后所有通道关闭 |
| 准入 | 同用户双窗口、登录/匿名访问者竞争、游客恢复与伪造恢复、重复请求、握手卡住、启动失败、旧 lease 迟到释放、Service/Render 重启恢复 |
| 会话保留 | Render 宽限退出、主动停止、异常退出、再次访问；核对 SID/Session 和应用状态；不存在时不能伪造恢复成功 |
| 画面 | 首帧、静态/滚动/视频、AVC420/444 实际协商、resize/全屏/DPI、最小化恢复、客户端 GPU 资源重建、独立光标 |
| 输入 | 扫描码/Unicode/中文输入路径、组合键、滚轮/额外鼠标键、失焦/断连按键释放、旧连接输入拒绝；不回退宿主 SendInput |
| 音频 | 实际听音、静音/恢复/设备变化、无重复播放；RDP 音频不绕宿主扬声器+loopback，不重复进普通模式音频通道 |
| 剪贴板/文件 | Unicode/HTML/DIB、双向文件/目录、空文件、重名、长路径、取消/断线、校验和、超限/恶意路径、重解析点与 ACL |
| 通道隔离 | 禁用功能不能只隐藏 UI；验证服务端/代理/RDS 执行策略，不默认暴露宿主盘、剪贴板、麦克风或全部设备 |
| 网络与协议 | 分包/粘包、部分写、慢读端、背压、长度溢出、连接中止、延迟/短时丢包；不得无界缓存或任意丢 RDP 字节 |
| C++ 生命周期 | 初始化每个失败点、排队后销毁、派发中注销监听、回调内停止、取消认证等待、重复 start/stop、旧 generation 回调 |
| 既有功能回归 | desktop/game-hook/webview 的启动、视频/输入、文件/剪贴板、既有超时、Native/Web 边界均不被新模式改变 |

完整文件管理器不等于剪贴板传文件。P6 先核对现有 FT 的目录列表/上传下载/取消/权限语义，
选择可限制到目标工作区的 RDP 映射或受限用户代理；不能继续用宿主权限执行文件操作并标为“目标用户文件”。
账号删除、管理员主动 logoff、RDS 全局策略变化和真实硬件设备测试需要明确范围，不能为了验收顺带改其他用户环境。

## 7. 每批不超过 10 分钟的验证安排

不是把全部功能塞进一次 10 分钟，也不是先跑长压测。每次实际测试批次连同收尾硬上限 10 分钟，
单项有子超时，超过预算标为未完成；完成一批再按需要执行下一批，不自动循环成长期常驻测试。

| 批次 | 分钟安排（包含证据与收尾，合计 10 分钟） |
|---|---|
| T1 原型/主流程 | 1 环境核查 + 2 登录首帧 + 2 键鼠/resize + 2 超时退出/重连 + 2 会话/进程证据 + 1 余量 |
| T2 独占/安全 | 2 身份/票据 + 2 同工作区争用 + 2 A/B 工作区 + 2 撤销/旧回调 + 1 收尾 + 1 余量 |
| T3 桌面通道 | 2 听音 + 2 文本/HTML/DIB + 3 文件/目录校验及取消 + 1 显示恢复 + 1 收尾 + 1 余量 |
| T4 性能 | 1 基线 + 2 单会话动态内容 + 2 双工作区 + 2 resize/重连 + 2 报告/收尾 + 1 余量 |

本机运行 Console 与 Client，90 运行 Service/Render/RDS；只有在对应实现就绪并获得实施任务后才部署或测试。
准备授权的专用测试工作区，不读取或输出已有秘密，不注销无关会话；结束时关闭测试 Client/Render，保留按产品规则应保留的 Windows 会话。
多屏、打印机、智能卡、触摸/笔等按独立短批次验收，缺设备标记未测；不宣称四批已覆盖所有外围硬件。

记录提交/配置、节点、非敏感工作区标识、状态时间线、准入结果、进程退出、会话保留、有效帧率和错误。
P0 验证不重编码要结合模式调用路径与代理统计，不能只看 Render GPU 利用率低；P7 使用动态内容核对有效呈现帧率。
demo 的 EndPaint 到 paintGL 计时不是输入到屏幕的端到端时延，旧隐藏窗口样本也不能用于 60fps 验收。
不引入重编码后不再测“GammaRay 视频编码耗时”，改测 RDS 输出、代理队列、链路、客户端解码/合成/显示瓶颈。

## 8. 构建、交付与回退

- 源码版本固定，不将 `D:/dolit/rdp` 绝对路径变成生产构建依赖；迁入自有模块记录来源，第三方依赖按仓库约定固定。
  不机械修改第三方源树；确需补丁时列最小变更、测试和升级风险后单独审查。
- 新增/迁入 C++ 按 `docs/cpp_smart_pointer_standard.md` 实现：确定性初始化、RAII、弱引用异步回调、Qt 单一所有权、150 列。
  FreeRDP ABI 只在最小适配边界出现，不借“demo 原来如此”新增裸指针生命周期债务。
- C++ 用 `scripts_build/build_cpp_client.bat`、`build_cpp_render.bat`、`build_cpp_sdk.bat` 及聚焦测试入口；
  新测试可由 `build_cpp_tests.bat <target>` 调用，不运行 release-only `build_official.bat`。
- Rust 针对 `service_core`、`px_service`、`px_console_server` 相关用例做测试/构建；Console Web 做 type-check、聚焦单测和实际改动对应构建。
  生成协议走源 `.proto` 的既有流程，不只手改生成文件；并验证旧字段/旧端能力拒绝行为。
- 变更 Client exe、FreeRDP/WinPR/依赖 DLL、语言资源/相关资产均同步到 `build_official/dist`，逐项 SHA-256 一致才交付。
  文件占用时停止准确对应进程再发布并复核；90 的服务端部署也记录源/目标版本及哈希，不用旧 DLL 混测。
- 每阶段独立变更与验证记录，提交/push 按后续用户要求执行；不修改无关脏文件，不移除现有归档。
- 回退通过停止新 RDP 运行实例、关闭入口/撤回运行产物，不删除已创建的持久账号/profile 或注销用户。
  数据字段尽量向后兼容；不为回退恢复已经退役的 Native RTC/Relay/WS 视频实现。

## 9. 当前交接

### 2026-09-09 更新

Console 工作区凭证、Service 标准账号、Render 原生代理及 Client Qt 6 已接入产品既有 WebSocket。
两个应用+节点分别获得独立标准用户及 RDS Session；原工作区在运行时重建后保留原 SID、登录时间和会话。
输入/resize/全屏、音频和双向文本/HTML/图片/文件目录已有旧 SDK 的短批次实测证据，
官方 3.31.0 的 MF 首帧问题现已通过固定补丁修复，且有未打补丁失败/补丁版成功的同一用例对照；
新版画面/resize/剪贴板/文件/音频及 Panel RDP 入口已有短批次通过证据。
自然退出消息唤醒、Service 查询锁竞争及 RDP 路由晚释放已修复并通过短批次回归。
运行期间授权撤销已接入既有 Service/Console WebSocket，实际撤销后 1.93 秒关闭连接且保留 Windows 会话；
正常连接、同逻辑 owner 快速重连及自然宽限退出均复验通过。部署新协议必须先 Console 后 Service。
Panel 已实现关闭 Client 后短暂窗口内、由用户再次访问触发的续票恢复（不是自动重连），
实机已验证沿用原逻辑会话、宽限期间重开及最终自然退出。外围硬件和既有 game-hook 问题按进度记录继续推进，
不能将这些子项推导为 P0–P7 全部完成。
最新 10:34 后双工作区复测触发/伴随 90 `dwm.exe/dwmcore.dll` 连续崩溃，随后单工作区也无首帧；
绕过产品链路的原生 RDP 直连期间同样有崩溃，系统图形栈根因待转储分析，当前真实桌面验收暂停。
不得通过注销工作区或重启 RDS 来掩盖故障；转储采集、驱动/策略调整和重启按明确授权进行。
后续 10:51 用户明确授权仅注销一个测试会话：B 的 Session 4 注销后以原 SID 新登录 Session 8，
真实桌面恢复，普通重连/自然退出复测通过；其他用户会话未操作。重连期间仍记录一次 DWM 崩溃，
这是一次性授权恢复而非根因修复，不能转成产品自动注销策略；A 和双工作区的完整恢复尚未验证。
最新事实及未测项以 [实施进度](rdp_implementation_progress_20260908.md) 为准。
11:35 恢复检查确认 90 已于 11:23 重启；原 A/B 会话不在。B 再登录后图像和 Panel 续票恢复通过，
但 Windows 使用临时 profile，原 SID 的 profile 带 `.bak` 且原目录缺少 `NTUSER.DAT`。
工作区持久化不能标为通过；保留现场，profile 修复及必要的该用户注销仍需单独授权。
Rust 聚焦回归入口为 `scripts_build/test_rdp_rust.bat`；真实桌面操作需本机交互桌面可用。

### 保留：最初的原型交接

已完成独立 proxy 编译、90 原生 SSPI 连接、AVC444v2 桌面和原会话重连；demo 增加了自动凭证启动支持。
最新用户决策是应用+节点账号、console 凭证权威、允许未登录自动准入、复用现有 WebSocket。详细证据见实测记录。
P0 已新增 `px_rdp_stream`：现有 `px::Message` 增加 `kRdpStream`，实现 32 KiB 分块、代次隔离、
有界接收队列、发送完成后继续读取、错误关闭与弱引用生命周期；新增单元测试及限时独立 WebSocket 探针。
90 实测通过新封装完成两次自动连接，协商 AVC444v2，退出后仍保留同一 Windows 会话。
探针复用项目 asio2 实现及消息封装，但尚未接入产品 `WsServer/WsConnection` 的既有连接、准入和控制消息调度，
不能把独立探针端口当作新增产品连接要求，也不能据此把 P0 全部门禁或 P1/P2 标为完成。
详细结果及未通过项见 [WebSocket 适配验证记录](rdp_websocket_adapter_validation_20260908.md)。
下一项是接入 SDK/Render 的既有 WebSocket 路由及受控实例绑定，随后迁入 Client Qt 6 RDP 工作区并完成真实画面验收；
Console 凭证、Service 账号/节点独占与 Render 退出宽限仍按阶段实现。
90 继续作为远端，禁止用 Administrator 登录 RDP；每批测试与收尾不超过 10 分钟。
其余工程细节按上述默认方案实施，不因已确定的账号粒度、自动授权、TCP 可靠性再次等待产品确认。

上游核对入口：[FreeRDP 代理配置 API](https://pub.freerdp.com/api/group__proxy__config.html)、
[FreeRDP 3.26.0 发布记录](https://www.freerdp.com/2026/05/06/3_26_0-release)。
实现细节以记录的本地提交为准，公开 API 当前页面不代替固定版本源码与实测。
