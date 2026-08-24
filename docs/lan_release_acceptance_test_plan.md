# 本机与 90 号机发布验收测试计划

> 状态：执行中且 LAN RC 阻断（2026-08-24 已完成 L0/L1 核心门禁、LAN Standard/Host、TURN/UDP、TURN/TCP 和 WebClient 虚拟显示器闭环；STAB-01 为 FAIL；公网项仍阻塞）
> 适用范围：当前没有公网/异运营商环境时的提交门禁、局域网发布候选验收和故障回归
> 测试拓扑：开发机作为主控与 Console/Coturn 节点，10.0.0.90 作为被控 Windows 实机
> 结论边界：本计划通过后可声明“功能、安全、生命周期、LAN TURN 和受控故障验收完成”，不得声明真实公网、对称 NAT 或跨运营商验收完成。

### 本轮执行快照（2026-08-24）

| 范围 | 实测结果 | 证据/备注 |
| --- | --- | --- |
| Service Rust | PASS，52/52 | 本机正式测试入口 |
| Console Server Rust | PASS，140/140 | 包含RTC、权限、生命周期和记录模块 |
| Ticket Mongo并发 | PASS | 20并发兑换仅1成功；20并发续票仅1成功；错误绑定均拒绝；临时库已删除 |
| Console Web | PASS，15/15并完成生产构建 | 单元与构建门禁 |
| WebClient语音状态机 | PASS，19 assertions | `npm test` |
| WebClient Standard信令/统计 | PASS，9/9 | 超时清理、迟到消息、并发Offer、有界ICE、relay/TCP统计 |
| WebClient生产构建 | PASS | Vue类型检查和Vite生产构建 |
| Standard RTC Host | PASS | R90真实Render；1920×1080；6秒解码+30帧；RTT 2 ms；0丢包/冻结 |
| TURN/UDP | PASS | 强制relay；selected candidate为relay/UDP；真实画面持续采集 |
| TURN/TCP回退 | PASS | API仍下发UDP+TCP，服务端关闭UDP listener；自动选中relay/TCP；6秒解码+30帧；RTT 1 ms；0丢包/冻结 |
| 虚拟显示器WebClient | PASS | 1屏→2屏、采集新增屏、切回物理屏、删除后恢复1屏；五阶段均持续解码 |
| STAB-01 连续连接/退出 | FAIL，97/100 | 复用同一用户，每轮独立ticket和Chrome profile，9秒媒体窗口；第45、90轮连接后解码帧增长0，第56轮30秒内未连接；其余97轮稳态0新增丢包/冻结；总耗时23.1分钟；最终用户/session/ticket均为0 |
| STAB-04/05/06 长稳、断网耐久、跨夜 | NOT-RUN | 按“某层失败即停止依赖项”规则，在STAB-01确定失败后停止；修复STAB-01并干净复测100/100后再执行 |
| 自动Direct、Direct失败回退、ICE restart、音频/输入/剪贴板/文件、Service重启 | PASS（既有本轮报告证据） | 详见 `docs/webrtc_rtc_acceptance_report_20260824.md`，仍列入发布前复跑清单 |
| 不同公网/NAT、对称NAT、公网443/TLS | BLOCKED-ENV | 当前没有公网环境，不以LAN仿真冒充通过 |

本轮自动化使用随机测试用户、一次性ticket和独立Chrome profile；100轮使用单一测试用户避免把注册限流混入RTC耐久，每轮仍使用独立ticket。最终用户、session、ticket计数均回到0，Chrome测试进程/profile为0，故障注入已恢复TURN双栈，虚拟显示器`owned`从0恢复到0。100轮前的两批无效样本（Chrome子进程未回收导致资源污染、每轮注册导致HTTP 429）只用于修复测试执行器，不计入产品通过率。

## 1. 目标

本计划覆盖最近功能提交中可以在当前环境完成的测试，并将已有人工诊断收敛为可重复、可自动判定、可清理的验收流程：

1. 所有新增专项测试进入统一构建和测试门禁。
2. 一次性 ticket、续票、权限和会话撤销具备并发与重放安全证明。
3. RTC Direct、RTC Standard、TURN UDP、TURN TCP、自动回退、ICE restart 和统计展示形成闭环。
4. 画面、系统声音、语音、输入、剪贴板、文件和多显示器在真实标准 RTC 上通过。
5. Console、Coturn、Service、Panel、Render 和 Client 的退出、异常、重启和恢复行为可判定。
6. 虚拟显示器、语音、文件传输、WebView、用户权限、监控和审计具备相应回归。
7. 通过受控阻断和网络仿真覆盖 UDP 不可用、节点不可达、断网、弱网、资源耗尽和恢复。
8. 所有测试保存脱敏证据，并在失败或中断后恢复防火墙、配置、进程和显示拓扑。

## 2. 当前不能宣称完成的公网项目

以下项目必须在取得外部环境后单独执行，局域网仿真不得代替：

| ID | 外部门禁 | 原因 |
| --- | --- | --- |
| PUB-01 | 两台机器位于不同公网出口/运营商时的真实 relay candidate | 当前两端处于同一局域网 |
| PUB-02 | 对称 NAT、CGNAT、双层 NAT 下的 srflx/relay 行为 | 局域网规则不能复现运营商映射特征 |
| PUB-03 | 企业代理、真实公网 UDP 限制和跨运营商路由 | 需要真实外部网络策略 |
| PUB-04 | 公网 TURN/TCP 443 或 TURN TLS 穿透 | 需要公网域名、证书和端口环境 |
| PUB-05 | 真实公网 MTU、QoS、长距离抖动和拥塞 | 本地仿真只能证明状态机和队列行为 |

公网环境到位后复用本计划的功能断言、统计字段、证据格式和清理要求，只替换网络拓扑。

## 3. 测试分层

| 层级 | 环境 | 内容 | 是否阻断 LAN 发布候选 |
| --- | --- | --- | --- |
| L0 | 本机 | 编译、格式、单元、协议、纯状态机 | 是 |
| L1 | 本机服务栈 | Console API、数据库、ticket、Relay、Service 集成 | 是 |
| L2 | 本机 + 90 | 真实浏览器/原生客户端、Render、Panel、驱动 | 是 |
| L3 | 本机 + 90 + 故障注入 | 端口阻断、进程退出、配置热更新、资源耗尽 | 是 |
| L4 | 本机 + 90 + 测试网关 | 延迟、丢包、乱序、限速、MTU、短时断网 | P0基线阻断，P1质量项记录风险 |
| L5 | 不同公网/NAT | PUB-01 至 PUB-05 | 当前外部阻断项 |

## 4. 环境、基线和安全

### 4.1 角色

| 角色 | 默认位置 | 主要组件 |
| --- | --- | --- |
| 主控/控制面 | 开发机 | px_console、px_auth、px_turn、WebClient、原生客户端、测试驱动 |
| 被控端 | 90号机 | px_service、px_panel、desktop px_render、USBMMIDD |
| 浏览器 | 开发机 | Chrome为P0，Edge为P1；使用独立测试profile |
| 网络仿真网关 | 可选虚拟机 | Linux tc netem 或等效受控网关 |

所有地址、端口和安装目录从测试参数或实际配置读取。不得把常用端口静默写死在断言中。

### 4.2 每轮执行前记录

- Git commit、分支、工作区状态、测试时间。
- Console、Coturn、Service、Panel、Render、WebClient、原生客户端和测试程序 SHA-256。
- 两机 Windows 版本、GPU、网卡、交互 Session、物理/虚拟显示器基线。
- RTC revision、ICE Server 摘要、TURN listener 和 relay 端口范围；不保存 credential 或共享密钥。
- Service、Panel、Render、Coturn PID、启动时间和监听端口。
- 90号机麦克风隐私、默认通信设备、USBMMIDD PnP/Driver Store 状态。

### 4.3 安全规则

1. 密码、cookie、appkey、ticket、renewal token、TURN credential、IPC token和完整 SDP 不进入 Git、报告、截图或命令历史。
2. 测试凭据只在运行时注入，输出前统一掩码。
3. 防火墙规则使用本轮唯一前缀，精确限定协议、端口、程序和对端地址；禁止关闭整个防火墙。
4. 修改麦克风隐私、RTC配置、TURN端口、显示拓扑或服务方式前保存原值，并在 finally 中恢复。
5. 只结束路径和命令行均属于本轮部署的进程，不按进程名批量结束未知实例。
6. 每个故障用例先验证目标 PID、服务名、规则名和配置路径。

## 5. 统一自动化入口与证据

建议建立统一入口 scripts/run_lan_release_acceptance.ps1，支持以下参数：

    -ControllerHost <controller>
    -TargetHost <target>
    -Layers L0,L1,L2,L3
    -Cases RTC-LAN-01,RTC-LAN-02
    -ResultRoot tests/artifacts/lan-release/<date>-<commit>

入口要求：

- 支持按层级、功能和用例ID选择。
- 每项有独立超时，失败返回非零退出码。
- 可从失败项继续，但不能复用一次性 ticket。
- 每项写 case-result.json，总入口写 summary.json 和 JUnit XML。
- 使用 try/finally 恢复防火墙、RTC配置、服务、隐私和显示基线。
- 清理失败本身算失败。
- 条件跳过标记 SKIPPED 并说明原因，禁止计入 PASS。

单项结果至少包含：

    case_id, result, commit, started_at, duration_ms
    rtc_mode, ice_revision
    local_candidate, remote_candidate
    rtt_ms_p50, rtt_ms_p95
    video_frames_delta
    cleanup, evidence

## 6. L0 构建和正式测试门禁

### 6.1 入口补齐

| ID | 检查 | 通过条件 |
| --- | --- | --- |
| GATE-01 | build_official_tests.bat 构建 test_voice_call | 目标存在且构建成功 |
| GATE-02 | 构建 test_client_voice_call_protocol | 目标存在且构建成功 |
| GATE-03 | 构建 test_client_virtual_display | 目标存在且构建成功 |
| GATE-04 | run_tc_tests.bat 或 CTest 运行上述目标 | 任一失败导致总入口非零 |
| GATE-05 | WebClient提供统一测试命令 | 语音、RTC信令、stats、虚拟显示状态均运行 |
| GATE-06 | 条件真实设备测试单独分组 | 普通CI不把跳过显示为通过 |

### 6.2 每次提交必跑

1. px_service、service_core、px_user_proxy Rust 全量测试。
2. px_console_server Rust 全量测试。
3. Console Web Vitest、类型检查和生产构建。
4. WebClient单元测试、类型检查和生产构建。
5. C++ common、文件、记录、语音、原生虚拟显示和语音协议测试。
6. px_client、px_panel、px_render、net_rtc、net_rtc_local、voice_call增量构建。
7. 安装汇总检查必需DLL、Web资源、USBMMIDD和Coturn产物。
8. git diff --check，并记录测试前后工作区差异。

## 7. Ticket、会话和权限

这些用例使用隔离数据库和真实 Manager/Handler，不得只验证纯函数或前端 mock。

| ID | 场景 | 步骤摘要 | 通过条件 |
| --- | --- | --- | --- |
| TKT-01 | 并发兑换 | 同一ticket同时20次redeem | 严格1次成功，其余已使用/过期 |
| TKT-02 | 并发续票 | 同一renewal同时20次renew | 严格1次成功，只产生一条新链 |
| TKT-03 | 设备绑定 | 错device ID兑换 | 失败且不泄漏绑定信息 |
| TKT-04 | nonce绑定 | 空、非法、不同nonce | 全部失败；正确nonce仍能兑换一次 |
| TKT-05 | instance绑定 | 桌面票与应用实例票交叉使用 | 失败，不能降级到设备票 |
| TKT-06 | 过期边界 | 到期前1ms和到期时刻 | 按统一时钟模型确定成功/失败 |
| TKT-07 | 会话撤销 | 签票后撤销、禁用或删除用户 | redeem、renew、资源查询均失败 |
| TKT-08 | 信令预检 | Relay lookup后Render redeem | lookup不消费；Render成功一次 |
| TKT-09 | 跨通道重放 | 同一票用于WS、Relay、RTC | 只有首个正式消费者成功 |
| TKT-10 | 权限最小化 | view/file/control票分别连接 | 只能创建获准通道和操作 |
| TKT-11 | 数据库错误 | issue/renew/redeem阶段注入失败 | 不产生两张有效票或伪成功 |
| TKT-12 | 审计脱敏 | 成功、失败、重放、绑定错误 | 有request ID和原因，无原始凭据 |

真实权限矩阵：

| 主体 | 设备列表 | 应用列表 | view | input | clipboard | file | audio |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 正常登录用户 | 全部注册设备 | 仅公开/ACL允许 | 按票 | 按票 | 按票 | 按票 | 按票 |
| view-only入口 | 全部注册设备 | 仅允许应用 | 是 | 否 | 否 | 否 | 否 |
| guest | 按公开策略 | 仅公开应用 | 按策略 | 默认否 | 否 | 否 | 否 |
| disabled/deleted/revoked | 否 | 否 | 否 | 否 | 否 | 否 | 否 |

## 8. Web标准RTC单元与集成测试

为 standard_signaling、路由选择、ticket轮换、ICE restart和stats提供可注入的 WebSocket、PeerConnection、时钟和fetch。

| ID | 场景 | 通过条件 |
| --- | --- | --- |
| RTC-UT-01 | Relay连接超时 | socket、heartbeat和callback全部清理 |
| RTC-UT-02 | 建房失败/Relay error | 错误只上报一次，无孤儿定时器 |
| RTC-UT-03 | room prepared迟到 | 已超时连接不能复活 |
| RTC-UT-04 | 本地ICE早于room/offer | 有界缓存，offer后按序发送 |
| RTC-UT-05 | 远端ICE早于answer | remote description后再add candidate |
| RTC-UT-06 | answer超时、重复、迟到 | 单一终态，不污染下一轮 |
| RTC-UT-07 | 并发exchangeOffer | 第二次被拒绝，不覆盖第一轮 |
| RTC-UT-08 | Direct建连超时 | 只切一次Standard，先轮换ticket |
| RTC-UT-09 | Standard继续失败 | 受最大重连次数约束 |
| RTC-UT-10 | 用户主动退出 | 回退、重连、poll和watchdog全部取消 |
| RTC-UT-11 | 相同/旧revision | 不触发SetConfiguration/restart |
| RTC-UT-12 | 新revision | 新ICE Server和iceRestart offer生效 |
| RTC-UT-13 | renew/配置/offer失败 | 完整重连或明确失败，无假connected |
| RTC-UT-14 | stats无selected pair | UI显示未知值，不抛异常 |
| RTC-UT-15 | candidate切换 | host/srflx/relay、协议、节点、RTT同步 |

## 9. 局域网RTC选路和TURN

每项必须验证实际 selected candidate，不得只看配置或Coturn日志。

| ID | 场景 | 配置/故障 | 通过条件 |
| --- | --- | --- | --- |
| RTC-LAN-01 | 自动Direct | 开启probe，90可达 | mode=direct，画面和DataChannel可用 |
| RTC-LAN-02 | Standard host | 强制Standard，policy=all | selected pair为host |
| RTC-LAN-03 | TURN UDP | policy=relay，保留UDP | selected pair为relay/UDP |
| RTC-LAN-04 | TURN TCP回退 | 精确阻断TURN UDP，保留TCP | selected pair为relay/TCP |
| RTC-LAN-05 | 禁止端到端直连 | 阻断Direct/Render，保留Console/TURN | 经TURN完成业务 |
| RTC-LAN-06 | 节点故障转移 | 一不可达TURN加一可达TURN | 使用可达节点，无超时放大 |
| RTC-LAN-07 | 所有TURN不可达 | 强制relay并停用节点 | 限时明确失败 |
| RTC-LAN-08 | Coturn停止不影响Direct | 停止px_turn后走Direct | Direct正常，Console状态准确 |
| RTC-LAN-09 | Direct实际失败 | probe成功后阻断Direct | 换新ticket并重开Standard |
| RTC-LAN-10 | 活跃配置更新 | 连接中提交更高revision | ICE restart完成，媒体恢复 |
| RTC-LAN-11 | 删除活动节点 | 移除当前TURN并添加可达节点 | 切换或明确重连 |
| RTC-LAN-12 | 凭据轮换 | 缩短TTL后ICE restart | 使用新credential且日志不泄密 |
| RTC-LAN-13 | 统计展示 | 每2秒采样stats | mode、revision、候选、协议、RTT一致 |
| RTC-LAN-14 | 主动退出 | 用户点击退出 | 不错误重连，无room/allocation残留 |
| RTC-LAN-15 | Peer异常 | 关闭PeerConnection或信令 | 新ticket重连，旧资源释放 |

阻断用例保存前后防火墙快照，清理后规则数量必须回到基线。

## 10. 标准RTC全功能

RTC-LAN-02、03、04各执行核心功能；RTC-LAN-03执行完整集合。

| ID | 功能 | 操作 | 通过条件 |
| --- | --- | --- | --- |
| FUNC-01 | 视频 | 首帧后采样120秒 | 分辨率正确、帧增长、无永久黑屏 |
| FUNC-02 | 系统声音 | 播放受控刺激 | 独立audio track有数据 |
| FUNC-03 | 输入 | 鼠标、按键、组合键、相对移动 | 被控收到，input RTT有回报 |
| FUNC-04 | 剪贴板 | 双向唯一文本和ack | 内容一致，无回声循环 |
| FUNC-05 | 文件上传 | 随机内容和SHA-256 | 大小、摘要一致 |
| FUNC-06 | 文件下载 | 下载远端文件 | 大小、摘要一致 |
| FUNC-07 | 文件续传 | 中断后恢复 | offset有效，临时文件清理 |
| FUNC-08 | 新增显示器 | 创建USBMMIDD屏 | 拓扑N到N+1，generation递增 |
| FUNC-09 | 多屏切换 | 切到新屏播放动态刺激 | 新屏独立帧持续增长 |
| FUNC-10 | 删除恢复 | 删除本轮屏并切物理屏 | 拓扑精确恢复，继续出帧 |
| FUNC-11 | 并行业务 | 视频+系统声+语音+输入+大文件10分钟 | 控制及时、队列有界 |

## 11. 语音通话

### 11.1 自动化状态和协议

| ID | 场景 | 通过条件 |
| --- | --- | --- |
| VOICE-UT-01 | 本地取消与远端接受竞态 | 单一终态，无孤儿端点 |
| VOICE-UT-02 | 断线、撤权、接管竞态 | 只清理一次，无锁反转 |
| VOICE-UT-03 | 错call/request/stream媒体 | 丢弃，不影响当前通话 |
| VOICE-UT-04 | 挂断后重放旧消息 | 不重新打开设备 |
| VOICE-UT-05 | 浏览器拒绝权限/无设备 | 不发呼叫、不建sender |
| VOICE-UT-06 | track ended/中途撤权 | 明确结束，远端释放 |
| VOICE-UT-07 | 每次安全提示 | 采集前提示暂停远控声音；取消零采集 |

### 11.2 本机与90真实链路

| ID | 场景 | 通过条件 |
| --- | --- | --- |
| VOICE-E2E-01 | Panel接受 | 双向RTP/PCM增长，UI Connected |
| VOICE-E2E-02 | Panel拒绝 | 两端Idle，track结束，声卡释放 |
| VOICE-E2E-03 | 30秒超时 | 无晚到授权，资源释放 |
| VOICE-E2E-04 | 主控取消 | Panel卡片关闭，旧点击无效 |
| VOICE-E2E-05 | 麦克风静音/恢复 | 不重建，远端由静音恢复 |
| VOICE-E2E-06 | 扬声器静音/恢复 | 仅语音下行变化 |
| VOICE-E2E-07 | HTTP不安全源 | 明确要求HTTPS，不调用getUserMedia |
| VOICE-E2E-08 | Windows隐私拒绝 | no_mic/permission denied，无假连接 |
| VOICE-E2E-09 | Panel/Render退出 | 对端结束并释放麦克风 |
| VOICE-E2E-10 | 原生客户端 | 呼叫、静音、挂断、设备选择全流程 |
| VOICE-E2E-11 | 虚拟屏拓扑变化 | 无假Connected或残留开麦 |
| VOICE-E2E-12 | 2小时真实端点 | 包数增长，资源和队列不越界 |

双物理终端外放AEC、ERLE、双讲和主观音质仍是实验室设备门禁。

## 12. 虚拟显示器

| ID | 场景 | 通过条件 |
| --- | --- | --- |
| VD-01 | 基线查询 | owned、maximum、generation与PnP一致 |
| VD-02 | 新增 | 只增加一块owned显示器 |
| VD-03 | 重复请求 | 相同request ID不重复操作 |
| VD-04 | 多客户端并发 | request ID不冲突，结果不串窗口 |
| VD-05 | 新屏采集 | 独立动态画面和持续帧 |
| VD-06 | 屏幕切换 | 物理/虚拟切换无永久黑屏 |
| VD-07 | 删除恢复 | 只删除本产品owned屏 |
| VD-08 | Service重启 | 状态从持久化与PnP恢复 |
| VD-09 | 驱动失败 | UI解除锁定并显示明确错误 |
| VD-10 | 慢Query不阻塞 | 阻塞5秒时heartbeat/ticket在500ms内返回 |
| VD-11 | 慢Create/Remove | AuthInfo、heartbeat、redeem继续工作 |
| VD-12 | 超时晚到 | 旧worker不能覆盖新结果 |
| VD-13 | 安装幂等 | 不重复创建设备/Driver Store包 |
| VD-14 | 卸载 | 服务、驱动、PnP节点和文件按策略清理 |

测试前后保存显示器、logical ID、generation、PnP InstanceId和第三方适配器状态；最终精确恢复基线。

## 13. 独立文件传输

| ID | 场景 | 通过条件 |
| --- | --- | --- |
| FT-01 | WS file-only | 只建立文件通道 |
| FT-02 | RTC data-only | 只有FT DataChannel，无媒体/输入 |
| FT-03 | Relay file-only | 只建立FT room，目标绑定正确 |
| FT-04 | 无票/错误票 | 三种通道全部拒绝 |
| FT-05 | ticket重放 | 首次成功后其他通道失败 |
| FT-06 | 权限隔离 | view票不能文件；file票不能输入/音频 |
| FT-07 | 复用活动客户端 | 不新增进程，不重连画面 |
| FT-08 | 无活动客户端 | 启动唯一file-only并显示根目录 |
| FT-09 | 首次目录请求 | 通道刚建立也能正确送达 |
| FT-10 | 文件业务 | 特殊字符、目录、覆盖、取消、续传、摘要通过 |
| FT-11 | 断线清理 | 作业、临时文件、通道和窗口正确清理 |

现有启动bat只能作为人工入口；正式验收必须自动判定、超时和清理。

## 14. WebView云应用

| ID | 场景 | 通过条件 |
| --- | --- | --- |
| WV-01 | URL编码和脱敏 | 中文/参数正确，日志无完整URL |
| WV-02 | GPU OSR | Canvas/WebGL持续变化 |
| WV-03 | CPU fallback | GPU不可用时明确降级 |
| WV-04 | 输入映射 | 坐标、点击、拖拽、双击、滚轮、组合键准确 |
| WV-05 | 文本 | 英文、中文、Emoji和粘贴按实现范围判定 |
| WV-06 | popup | 打开、选择、关闭、几何和焦点正确 |
| WV-07 | 观察者隔离 | 非控制者输入被拒绝 |
| WV-08 | 断连释放 | 无按键、鼠标键和焦点残留 |
| WV-09 | renderer/GPU crash | 有界恢复或明确Failed |
| WV-10 | Service/Console重启 | 状态收敛，profile lock和子进程回收 |
| WV-11 | 权限拒绝 | 摄像头、麦克风、文件选择按范围拒绝 |
| WV-12 | 30分钟稳定性 | FPS、输入p95和资源不持续恶化 |

不在产品范围的多点触控或虚拟剪贴板标为 N/A-by-design，不记为PASS。

## 15. Console、监控、审计和悬浮UI

### 15.1 Console清洁切换

Console重命名采用断代策略，不测试旧数据迁移：

| ID | 场景 | 通过条件 |
| --- | --- | --- |
| CONSOLE-01 | 删除旧数据后首次启动 | 生成新数据并初始化管理员 |
| CONSOLE-02 | 旧凭据失效 | 旧cookie/session/ticket/appkey均不可用 |
| CONSOLE-03 | 无旧组件残留 | 无旧exe、服务、进程、任务和端口 |
| CONSOLE-04 | 新数据持久化 | 新用户/设备/应用重启后存在 |
| CONSOLE-05 | 重复安装 | 不创建旧目录，不恢复旧数据 |

### 15.2 监控和直播

- 1、4、10个观察者并发进入/退出，无编码器或观察者残留。
- 观察者反馈不得改变控制会话码率、分辨率或帧率。
- 默认排序、自动播放和目标切换稳定。
- 5分钟延迟不单调增长；断开后15秒内恢复或明确失败。
- Console、媒体侧车和Render分别重启后状态收敛。

### 15.3 审计和事件

- 伪造设备上报者、跨设备记录和缺失认证全部拒绝。
- 重复start/terminal、乱序terminal和重试不产生多个成功记录。
- MongoDB不可用时不返回伪成功；恢复后不重复非幂等写入。
- telemetry合并在并发乱序下保持单一身份和最新值。
- 清理与查询/下载并发时不删除keep或活动记录。
- 拒绝和异常可按request ID定位且不泄密。

### 15.4 原生悬浮UI

- 100%、125%、150%、200% DPI不裁切、不重叠。
- 主屏/副屏、四边和四角停靠时菜单可见。
- 最小化、全屏、失焦、重连和拓扑变化后状态正确。
- 语音、虚拟显示、切屏、文件、断开按钮与真实能力同步。
- tooltip、accessible name、键盘焦点和高对比度可用。
- 多窗口请求、菜单和通话归属不串线。

## 16. 生命周期与故障注入

| ID | 故障 | 注入时机 | 通过条件 |
| --- | --- | --- | --- |
| LIFE-01 | 重启Console | 空闲、连接中、已连接 | Service重连，票据安全失效/重签 |
| LIFE-02 | 重启Coturn | 分配前、活跃allocation | 明确失败或ICE恢复 |
| LIFE-03 | 重启Service | 空闲、Render中、虚拟屏操作中 | 旧进程清理，新token链恢复 |
| LIFE-04 | 退出Panel | 来电等待、通话中、远控中 | 无授权漂移和资源残留 |
| LIFE-05 | 杀死Render | 建连、文件、通话中 | Client失败/重连，资源释放 |
| LIFE-06 | 关闭浏览器Tab | 待接受、Connected、文件中 | 远端释放麦克风和会话 |
| LIFE-07 | 网络阻断 | 信令前、ICE中、Connected | 有界重试，恢复后单一连接 |
| LIFE-08 | 连续接管 | 两客户端交替10次 | 只有一个控制者，旧输入/语音失效 |

记录恢复时间，并确认新旧PID、ticket、IPC token和RTC room不混用。

## 17. 网络仿真

若有Linux/Hyper-V测试网关，执行：

| ID | RTT | 丢包 | 乱序 | 抖动 | 限速 | MTU | 时间 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| NET-01 | 20ms | 0 | 0 | 0 | 不限 | 1500 | 10分钟 |
| NET-02 | 100ms | 1% | 0 | ±20ms | 20Mbps | 1500 | 10分钟 |
| NET-03 | 300ms | 5% | 2% | ±50ms | 5Mbps | 1300 | 10分钟 |
| NET-04 | 800ms | 10% | 10% | ±100ms | 2Mbps | 1200 | 5分钟 |
| NET-05 | 基线 | 100%断网 | - | - | - | - | 5/15/60秒 |

判定：

- NET-01、02必须保持连接和业务可用。
- NET-03允许质量下降，但不能崩溃、死锁、无限队列或永久黑屏。
- NET-04为极限记录项，必须明确降级/失败并可恢复。
- NET-05恢复后只有一个PeerConnection、一个语音sender和一条有效ticket链。
- 记录RTT、loss、jitter、bitrate、帧率、freeze、队列和输入p50/p95。

没有网关时，端口阻断仍为P0；弱网质量项标记 BLOCKED-ENV。

## 18. 容量和稳定性

| ID | 场景 | 规模/时间 | 通过条件 |
| --- | --- | --- | --- |
| STAB-01 | 连续连接退出 | 100轮 | 100%成功，无session/room/allocation增长 |
| STAB-02 | 多浏览器并发 | 10上下文 | 不串票、不串画面、权限正确 |
| STAB-03 | TURN端口耗尽 | 4至8个relay端口并超额建连 | 超额明确失败，释放后恢复 |
| STAB-04 | 全功能长稳 | 2小时 | 视频/音频/输入/文件并行稳定 |
| STAB-05 | 断网恢复耐久 | 每10分钟断15秒，共20轮 | 每轮恢复，无重连风暴 |
| STAB-06 | 跨夜 | 8至12小时 | 无崩溃、黑屏或资源持续增长 |

初始资源门槛：Private Bytes增长不超过稳定基线20%或批准的绝对值；句柄增长不超过32；线程、RTC room、TURN allocation和进程在清理后回到基线。若当前基线不满足，先建立可重复基线，不能放宽到无限阈值。

## 19. 执行顺序

1. 记录commit、哈希和两机基线。
2. 执行L0构建、单元、协议和状态机。
3. 执行L1 ticket、权限、Console API和Service集成。
4. 无故障完成Direct、Standard host、TURN UDP和全功能基线。
5. 执行TURN TCP、节点故障、Direct回退、ICE更新。
6. 执行语音、虚拟显示、文件和WebView专项。
7. 执行组件重启、端口阻断、并发和资源耗尽。
8. 有网关时执行NET矩阵。
9. 执行100轮、2小时和跨夜稳定性。
10. 恢复配置、隐私和显示基线，删除测试实体、规则、任务和临时文件。
11. 生成汇总，列出PASS、FAIL、SKIPPED、BLOCKED-ENV和公网门禁。

某层失败时先保存证据并停止依赖该层的后续用例，不能用后续偶然成功覆盖确定失败。

## 20. 发布判定

### 20.1 LAN RC PASS条件

1. L0、L1所有P0测试100%通过且连续干净复测两次。
2. Ticket并发、重放、绑定、权限和撤销无失败。
3. Direct、Standard host、TURN UDP、TURN TCP和Direct回退全部通过。
4. 标准RTC全功能通过，删除虚拟屏后物理画面恢复。
5. Web和原生语音呼叫、拒绝、超时、挂断、静音和释放通过。
6. Console、Coturn、Service、Panel和Render重启收敛且无旧资源。
7. 100轮连接退出和2小时全功能长稳通过。
8. 测试结束后防火墙、服务、RTC配置、隐私和显示拓扑恢复。
9. 结果和日志不包含敏感凭据。
10. 公网项目标记BLOCKED-ENV，未误报为PASS。

### 20.2 允许使用的结论

    当前提交已通过本机单元/集成门禁、本机与90号机真实功能验收、
    局域网TURN UDP/TCP、受控网络故障、组件重启和稳定性测试。
    真实异公网、对称NAT/CGNAT、跨运营商和公网TURN/TLS仍待外部环境验收。

不得使用“RTC生产网络全部验收完成”“所有NAT均支持”或“公网TURN已验证”等超出证据的表述。

## 21. 缺陷与报告模板

每个失败至少记录：

    用例ID：
    Git commit：
    环境/层级：
    开始/结束时间：
    预期：
    实际：
    首次失败步骤：
    RTC模式/revision/candidate/RTT：
    相关PID和端口：
    脱敏request ID：
    日志时间窗：
    截图/统计/结果JSON：
    是否可重复：
    清理结果：

总报告必须包含：

- PASS/FAIL/SKIPPED/BLOCKED-ENV数量。
- 失败和不稳定用例，不只给通过率。
- 每个组件的实际二进制哈希。
- 网络规则、RTC revision、显示器和进程的前后差异。
- 资源曲线、恢复时间、帧数、RTP/PCM、文件摘要和候选证据。
- 公网外部门禁和下一次执行所需环境。

## 22. 最终清理

1. 删除本轮用户、组、应用、实例、ticket、guest session和测试记录。
2. 停止本轮Client、Render、WebView实例；确认无测试命令行进程。
3. 删除本轮唯一前缀的防火墙、计划任务和临时证书/配置。
4. 恢复RTC revision、ICE Server、TURN listener和relay端口范围。
5. 恢复90号机麦克风隐私和默认通信设备。
6. 删除本轮新增USBMMIDD屏，核对owned、generation、PnP和第三方适配器。
7. 确认Console、Service、Panel、Render和Coturn按默认状态运行。
8. 确认端口、进程、RTC room和TURN allocation回到基线。
9. 扫描证据中的password、cookie、ticket、renewal、credential、secret、appkey和完整SDP。
10. 保存脱敏最终报告和机器可读结果。
