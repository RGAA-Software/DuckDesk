# P1/P2 发行隔离与最小授权执行计划

> 计划日期：2026-09-23。
>
> 前置状态：DB0–DB5 当前开发基线已按确认范围收口。本文是下一阶段实施入口，不是完成声明。

## 1. 阶段目标

本阶段只完成两件事：

1. **P1 发行隔离**：把 Official 与 Customer 的服务器指向、发行身份、配置存储、包身份和升级边界固定为可重复验证的产品行为。
2. **P2 最小授权**：保留且收紧 `PXLIC2` 服务端许可证能力，只用许可证决定客户可使用的 `services` 和最大并发 `max_streams`。

P1/P2 复用已经完成的 PostgreSQL、账号/ACL、资源会话、实例事务、节点协议和安装包基础，不重新设计数据库阶段，也不先拆 Broker/Relay。

## 2. 不可扩大范围的产品决定

- Official 客户端固定连接 Pixels 官方 HTTPS 服务，不提供服务器编辑入口。
- Customer 客户端必须由用户填写自己的私有部署地址；本地在发送账号、设备或会话信息前拒绝已知官方 origin。
- Official 与 Customer 的缓存、凭据、安装身份、更新命名空间和运行配置不得串用；二者不能共存安装。
- Windows 包继续明确为未签名安装包，支持安装、卸载、同发行覆盖和相邻版本升级；不建设 Windows 代码签名流程。
- Windows Server、节点与访问端采用运维人员逐个覆盖安装：先升级 Server，再升级节点和客户端。接口兼容由发布顺序和测试矩阵保证，
  不建设 A/B 服务槽、分布式升级事务或客户端自动追随官网升级。
- 授权只保留 `PXLIC2`。许可证由服务端签发和本地验证，当前字段只承担部署身份、有效期、`services` 和 `max_streams` 等必要约束。
- 不实现 PXDC/PXDC2/PXDD、客户端短期 ticket、在线 verify、撤销通知 outbox、Console currentness 监控或客户端许可证协议。
- Service 只消费 Console 已裁决的 control epoch/资源会话，不解析许可证，不复制一套授权状态机。
- 不恢复 Mongo、Redis、旧协议、旧 ID、旧配置、旧安装路径或兼容 fallback。

## 3. 本阶段明确不做

- P3 的 Broker 拆分、多 Render/多 Relay 资源池与多机调度。
- P4 的完整私有部署安装套件和离线交付向导。
- DB-HA、Console 多活、Kubernetes、云厂商自动扩缩容或活动 Relay 连接迁移。
- Windows 自动更新、节点 TUF 消费、热升级编排和跨节点升级协调。
- ZLMediaKit、Coturn/TURN、中央 WebRTC signaling、视频墙和直播能力。
- 已延期的 Android fast-release、AMD/Intel 物理 GPU、目标 Linux、独立对象存储灾难恢复和统一长测；这些仍是未来商业发布清单，
  不是本阶段实现入口。

## 4. 实施批次

### P1-0 现状盘点与契约冻结

- 列出 Windows Cloud Node、Client、Remote 与 Android 的 Official/Customer 配置入口、发行清单、版本文件和安装身份。
- 列出 Console、Auth、Desk、Relay 的部署身份和外部 HTTPS 入口，确认哪些是发行常量、哪些是 Customer 必填配置。
- 对照当前代码删除计划文档中已经退役的升级、签名和授权概念；不因文档历史恢复代码。

出口：形成唯一字段表和生产者/消费者表；同一个概念不能同时由环境变量、配置文件和数据库以不同默认值决定。

当前冻结的唯一来源如下；不存在运行时猜测或第二默认值：

| 字段/身份 | 唯一生产者与存储 | 消费者与约束 |
|---|---|---|
| Official Console origin | 正式构建输入 `PIXELS_OFFICIAL_CONSOLE_URL`；Windows 编入 `PX_OFFICIAL_CONSOLE_ORIGIN`，Android 编入 `BuildConfig.OFFICIAL_CONSOLE_URL` | Official 固定使用且隐藏编辑；Customer 只把同一规范 origin 当禁止值。客户端不从 Desk、数据库或旧配置推导官方地址 |
| Customer Console origin | 用户输入后规范化；Windows 保存 `console_server_url`，Android 保存 `console_endpoint_v1` | 只能是非官方 HTTPS origin；切换时 Windows 清理绑定旧 origin 的凭据，Android 清理持久账号会话和内存 guest 会话 |
| 客户端发行身份 | Windows/Android 一次正式构建事务生成的 distribution、release namespace、OEM/profile 身份与产品版本 | 产品清单、安装身份、更新查询和 TUF 目标共同消费；Desk 只登记已发布目标，不反向决定已安装客户端的发行身份 |
| 服务部署身份 | 运维显式提供的 `PIXELS_DEPLOYMENT_ID`，并由三库 fresh-schema 数据与 `PXLIC2` payload 绑定 | Console/Auth/Desk 各自启动校验；客户端不填写 deployment ID，资源归属由 Console 当前 API 决定 |
| Console 外部入口 | Console 进程配置 `PIXELS_CONSOLE_PUBLIC_ORIGIN` 与反向代理部署配置 | Console 生成同源 API/Web 行为；它不覆盖客户端发行包内的 Official origin，也不是 Customer fallback |
| Render/Relay 连接 endpoint | 当前节点配置、Console 实例/资源会话事务和 descriptor | Windows/Web/Android 只消费 descriptor；不得使用固定端口、旧地址或客户端本地默认值 |
| License entitlement | Auth 签发的 `PXLIC2` 文件；Console 从私有路径本地验签 | 仅 Console 将 `services + max_streams` 带入实例/资源会话事务；Desk、Service、Render 和客户端不复制授权状态 |

### P1-1 Official/Customer 配置边界

- Official 编译产物只接受官方 origin，运行设置页不允许改写。
- Customer 首次使用要求输入规范 HTTPS origin，验证 scheme、host、port 和路径规范化后保存到平台安全存储。
- 已知官方域名、官方 IP/origin 及其规范化等价值必须在发出登录请求前拒绝；不宣称识别任意客户反向代理。
- 切换 Customer 私有平台时清除或分区旧平台 token、账号、设备和页面缓存，迟到响应不得覆盖新平台状态。

出口：纯配置测试、负向 origin 测试、凭据分区测试及 Windows/Android UI 状态测试通过。

### P1-2 构建、版本与安装身份

- 继续使用各产品独立版本号；一次正式构建只递增对应产品一次，Official/Customer 共享该产品版本。
- 日常开发只运行聚焦构建并同步 development dist；阶段出口才执行 release-only 完整双发行构建。
- 清单核对产品、distribution、release namespace、OEM 身份、文件集合、依赖边界和 SHA-256。
- 安装器验证同发行覆盖/相邻升级成功，跨产品和跨发行返回冲突且不改变原安装；Windows 保持 unsigned 明示。

出口：Windows Cloud Node、Client、Remote 的双发行制品矩阵完整，版本事务、安装目录、卸载项和运行时清单一致。Android 本阶段只在其
代码受影响时执行 distribution-specific 聚焦构建和短测；fast-release 实物覆盖仍按已确认延期清单处理。

### P2-0 PXLIC2 契约收紧

- 冻结许可证规范编码、签名算法/key id、部署身份、有效期、`services` 和 `max_streams` 的唯一语义。
- 未知 service、重复字段、非法上限、过期、错误部署、错误签名和回滚许可证一律 fail-closed。
- 设备登记、用户数量等不属于 `max_streams`；只有成功占用的活动 stream 消耗额度。

出口：固定向量、篡改、边界、过期和 deployment 隔离测试通过，活动代码中不存在第二套许可证格式。

### P2-1 Console 授权裁决

- Console 在创建资源会话/实例的同一事务中检查 service entitlement 和 `max_streams`，避免并发超售。
- 重复 `request_id` 返回同一业务结果，不重复占用额度；启动失败、停止和终态收敛精确释放占用。
- 许可证失效后拒绝新 Start；已运行会话只按已冻结的产品策略和 control epoch 处理，不让重连隐式延长授权。
- 管理后台只显示许可证摘要、允许服务、上限、当前占用、到期和明确拒绝原因，不显示私钥或完整敏感材料。

出口：最后一槽并发竞争、重复请求、失败回滚、重启恢复、过期和服务越权均通过 PostgreSQL 集成测试。

### P2-2 消费端与跨发行闭环

- Windows Client、Web Client、Android、Service 和 Render 只消费 Console 当前 API/descriptor/control epoch。
- Official 客户端只能进入官方部署；Customer 客户端只能进入配置的私有部署，身份与资源 ID 相同也不能跨部署复用。
- Console/Auth/Desk 的职责保持独立；Auth 签发许可证，Console 本地验证并执行额度，Desk 不参与运行时授权。

出口：Official 与一个隔离 Customer 环境分别完成登录、目录、启动、停止和额度拒绝短测；交叉 token、相同 ID 和迟到响应全部拒绝。

### P1/P2-EXIT 阶段验收

- 先跑受影响模块的格式化、静态检查、单元/集成测试和聚焦构建。
- 再做一次短时真实功能矩阵；开发阶段不执行长时间稳定性测试。
- 最后一次性构建受影响 Windows 产品的完整双发行制品，核对版本、清单、SHA-256、安装身份和必要的相邻升级路径；不以此重新触发
  已延期的 Android fast-release 验收。
- 验收报告必须分别标记代码门禁、真实功能、制品门禁和延期项，不能用其中一类替代另一类。

## 5. 完成定义

满足以下条件后才进入 P3：

1. Official 固定官方服务，Customer 可配置私有服务且不能填写已知官方地址。
2. 两种发行的账号、token、设备、缓存、更新身份和安装身份互不串用。
3. `PXLIC2` 是唯一活动许可证格式，服务端正确执行 `services + max_streams`，并发与失败不超售、不泄漏。
4. Console/Auth/Desk/Service/Render 之间没有第二套授权判断，客户端没有许可证 ticket 或隐式授权 fallback。
5. 实际受影响的 Windows、Web、Android、Console/Auth/Desk 模块完成相应短测；Windows 完整制品可追溯到版本和 SHA-256，未修改的
   Android 不因本阶段收口被迫重做 fast-release。
6. 所有未做事项明确留在后续阶段或商业发布清单，没有以“暂未实现”伪装成功。

## 6. 2026-09-23 首批实施结果

本轮只关闭已经确认的代码级缺口，没有启动正式发布事务：

- Windows Panel 现在会先规范化打包注入的 Official origin，再用于 Official 固定地址或 Customer 禁止地址比较；主机大小写、尾斜杠和默认
  HTTPS 端口不再形成 Customer 绕过。聚焦测试还直接验证同 Console origin 保留账号绑定、切换到另一私有 origin 后清理旧账号绑定。Panel 产品测试
  与本地化测试通过，变更后的 `px_panel.exe` 已同步到 Client development dist，
  build/dist SHA-256 同为 `0FEA810A1ECD0CCCE78A8173241A67B2F415E865A00C3305C3FA140B0E9AACE7`。
- Android `core-network` 的 Official 固定地址、Customer 禁止 Official 地址及切换会话状态单测通过；本轮没有修改 Android 运行代码，也没有
  重新制作 APK。
- `PXLIC2` 固定向量、篡改、非法字段、deployment/到期绑定和 trust key 轮换共 5/5 通过；Console runtime 11/11 通过。Windows
  runtime 单测夹具补齐私钥目录 ACL，生产私钥读取规则没有放宽。
- PostgreSQL `sessions` 专项新增两个独立运行实例争抢许可证最后一个 stream 槽位的并发测试，13/13 通过，报告为
  `pg-20260923-184620-39ab2cb3`；Console directory API（含管理员许可证摘要门禁）7/7 通过，报告为
  `pg-20260923-185940-6f4796b3`。实例预留专项也以 16/16 通过，确认未授权 Cloud/RDP 服务在产生节点命令前拒绝、失败事务不占容量、
  最后一槽竞争保持原子，报告为 `pg-20260923-193242-e6303921`。受影响 Rust 包严格 Clippy 和 rustfmt 通过。
- 活动源码未发现 `PXDC2`、`PXDD2` 或 `PXDP1` 消费端协议；Web Client 继续从当前 Console 页面取得同源入口和资源 descriptor，不解析许可证。

Client development dist 的清理后恢复入口已补为 `scripts_build/build_client_development.bat`：它不升版、不生成安装包，先完整校验并复用版本固定的
RDP SDK，仅在缓存不再匹配时重建；随后通过产品 CMake 聚合目标增量构建 Panel、Client、`px_osinfo`，再由现有 collector 原子生成并验证 schema 4
development manifest。完整收集复核同时删除了 `artifact_groups.toml` 中误将 Web 旧图标重新映射为桌面顶层 `resources/icons/px_icon.png` 的条目，并将
该路径加入制品拒绝门禁；桌面只保留当前 `resources/icons/brand/` 品牌资源。本轮生成 Client 3.3.74 共 40 件，全部 SHA-256 和依赖边界通过；
`px_panel.exe`、`px_client.exe`、
`px_osinfo.exe` 的 build/dist 摘要分别一致，
development dist 的 Panel 隐藏启动 5 秒冒烟通过。

本轮仍**没有**声明 P1/P2 整体出口完成。按工作区规则，未获明确“完整/正式构建”指令时不运行 `scripts_build/build_official.bat`。
Official/Customer 完整双发行制品、安装生命周期和隔离 Customer 实环境短测仍属于 P1/P2-EXIT。

## 7. 下一阶段接口

P1/P2 完成后进入 P3：在不改变本计划发行身份和最小授权语义的前提下，推进 Broker/Relay 边界、多 Render/多 Relay 发现、容量门禁、
资源池和多机短测。P3 不得重新引入 PXDC2、中央 WebRTC signaling、TURN 或自动升级编排。
