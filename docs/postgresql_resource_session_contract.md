# PostgreSQL 资源会话契约与实施边界

2026-09-17。DB2-D 的字段与行为约束。资源会话 repository 已实现并进入专项测试，
尚未替换产品 HTTP/WS、真实节点和客户端，不能作为 DB2/DB5 验收通过声明。
对应[领域清单](postgresql_domain_contract.md)、[实例/命令](postgresql_instance_contract.md)和[工作区凭据](postgresql_workspace_contract.md)。

## 身份和目标

创建请求仅接受请求 ID、明确目标、终端能力与访问模式；owner 从本次通过验证的凭据获得，不从请求正文获取。
终端类型绑定登录/访客会话，Android 必须是 android。管理网页身份不能伪造用户资源会话。

| 目标 | 必须提供的资源身份 | 禁止行为 |
|---|---|---|
| Desktop | device UUID；当前用户必须有设备访问权限 | 访客借设备 ID 获取桌面；公开编号充当身份或口令 |
| CloudApplication | application UUID + instance UUID；实例必须属于当前 owner | 缺实例时用 device/account/user ID 补齐；实例与应用不匹配；猜别人的实例 |

表关系显式保存 owner_user 或 owner_guest（二选一）、login/guest session、授权版本、client_type、目标种类及 FK。
CloudApplication 的 node 是执行位置，不是业务目标；RDP 工作区的 node owner 也不替代访问主体。
端点、描述符 revision、transport 属于后续连接描述，不能放进对象 ID 中拼接或靠字符串格式猜模式。

## 生命周期和占用

资源会话与登录会话、应用实例、Windows 工作区是不同生命周期。持久记录不证明前端在线。
状态区分待连接、已确认连接、请求关闭、待对账和已关闭；断开/超时不直接推断节点侧已经没有连接。
RDP 同实例只允许一个前端，第二个明确 busy；没有自动接管或观察者。
其他模式只有目录允许时才能申请观察者，观察者不因此获得输入/控制权限。
会话关闭不直接删除实例或工作区；实际 Render 仍执行既定最后客户端离开宽限，RDP 不注销 Windows 会话/删除账号/Profile/应用。

命令启动 origin 作为不可变审计事实保留。原始准入已经撤销时，新的登录不能悄悄恢复旧 Start 或撤销正在执行的 Stop；
后续若需要跨登录续接仍存活的实例，必须实现明确的重新授权转换，不能用同 user/device ID 作为 fallback。
新授权重新预约 RDP 时仍复用同一工作区，并不创建另一套 Windows 身份。

## 描述符与授权

描述符只在事务内复查身份/授权、实例状态、节点 epoch/generation、端点 revision、新鲜度与实际端口后签发。
桌面使用当前报告的桌面端口；云应用使用当前实例确认的端口。没有缺字段默认地址、历史端点或旧端口探测。
Native 的 TCP/WS 和 UDP 使用同一实际 Render 端口；RDP 使用专用可靠承载，不引入媒体转码或 Relay 回退。
RDP 当前只接受 Windows/Panel 路径；Android 和网页不能因有通用字段就被标成支持 RDP。

创建逻辑会话和获取短期描述符分开：相同请求 ID 返回同一业务结果，过期描述符不能因为重试而直接复用。
新描述符有显式 revision、到期时间、目标与连接代际；晚到/旧 revision 拒绝。返回 host/port **不等于获得传输授权**。
最终连接适配器还须验证短期凭据和当前权限，落实关闭/撤销上限；缺适配器时不允许把描述符仓库标成可投入使用的连接功能。
Windows 凭据仅通过既定受保护 RDP/SSPI 准入边界交付，管理 DTO、普通描述符、日志和历史记录不包含明文或密文口令。

## 记录与验证

会话事件、连接/流、访问及传输记录关联资源会话；使用明确生产者身份、单调序号和幂等键。
节点本地录像库与会话录制事件分开：本地录制可以没有访问会话，文件在前端退出后仍可列出，
不能强制所有录像都引用 resource_session，也不能丢掉按节点查看、保留副本和缓存清理能力。
节点只能报告自己的会话，计数和状态有边界；历史字节数/PID/Windows Session ID 不是授权身份。
稳定分页、去密管理与 owner 查询使用同一权限基础；事件追加，不用可变 JSON 对象代替关系和审计。

验收至少包含 Android 明确 CloudApplication 目标、错 owner/应用/实例、访客与用户隔离、RDP busy、观察者限制、
描述符过期/端点变化/节点重连、原始授权撤销、创建与事件故障完整回滚、备份恢复后不可凭旧在线快照接入。
真实 Windows 后 Android 连接及节点关闭证明属于 DB5，不以仓库测试替代。

## 当前实现

- 新表 resource_sessions、resource_session_events、resource_session_retirements；目标复合 FK、owner 二选一、
  原始登录与授权版本、终端类型、同目标 controller 部分唯一索引。节点最多 128 个未关闭前端，每目标最多 32 个。
- 资源会话 ID 是逻辑前端身份。实际节点必须保证同 ID 只有一个前端通道集合，不能把多通道计为多人，
  也不能因重复 admission 请求创建第二个 RDP 前端。数据库仓库测试不能证明节点已经落实此约束。
- 当前描述符摘要/到期与 revision 直接保存在资源会话行，描述符签发事件追加；不另建 connection_descriptors 权威表。
  凭据由 composition adapter 用 CSPRNG 生成，只保存摘要，成功提交后才交给对应前端；管理查询不返回凭据、端点或原始登录 ID。
- descriptor 与 admit_frontend 均重新检查原始身份、目标权限、实例 Running、节点代际/端点和 30 秒新鲜度。
  每份授权租约最长 30 秒，续租必须使用原 frontend token 在线复查；成功复核在同一事务中把当前描述符租约推进 30 秒，
  后续策略或端点检查失败会回滚该推进。Render 必须在租约到期前续租，否则停止该精确 RTC allocation 并关闭逻辑绑定；
  Console 不可用只允许在当前租约内有界重试，不得无限延长，也不额外签发短期 ticket。
  admit_frontend 同时返回 PG 计算的剩余 valid_for_ms（1–30000）；节点以本次请求发出时的单调时钟加该值设置截止，
  不使用本机墙上时钟，不从响应到达时重新计满期限；排队/网络延迟不能延长租约。
- 同一 descriptor 的重复 admission 只有在完整在线授权复核成功后才续租；它不创建第二个逻辑前端，也不补签或替换 frontend token。
  新 descriptor CAS 提升 revision 并使旧凭据失效。重试 open 只返回同一逻辑结果。
- request_close 只转 Closing、失效凭据并保留占用。节点 begin_retirement 获得 30 秒 challenge 与包含式 revision fence；
  必须先持久化该前端 fence、排空正在处理的准入、关闭该前端的全部通道，再 finish_retirement。
  旧 challenge、旧节点、过期或变化的 revision 拒绝；确认重复提交幂等，不停止应用、不注销 Windows 会话。
- 节点/Console 重连、端点变更把未关闭前端置为 ReconcileRequired，清除授权；list_node 给当前节点完整有界待处理身份，
  包括从未成功交付的预约。节点可以对这些精确身份完成 retirement，不能仅因超时猜测已经无人。
- 排空拒绝新 open，但不撤销健康的既有租约。云应用 observer 仅在目录允许时准入，授权明确标记 observer，
  节点不能给予控制输入；RDP 不支持 observer。桌面 observer/takeover 尚待独立策略，当前明确拒绝，不宣称已覆盖旧产品全部行为。

完整报告 pg-20260917-054053-9eb21b04 的 Windows/Linux 各 10 组资源会话用例、单元、共享池及三库恢复通过，
源码/工具 hash 已核对。仍未覆盖真实节点单前端/单调时钟硬截止、产品协议和客户端。

2026-09-18 续租纵向切片已接通 PostgreSQL、Console 节点准入、Render 逻辑会话和本地 RTC：首次准入及周期续租都使用原
frontend token，不产生额外短期 ticket；Render 按 `valid_for_ms` 建立单调时钟截止，约三分之一租期发起复核。可重试故障只在
当前租约内有界重试，请求 deadline 不得越过租约截止；永久拒绝、身份/目标/revision 漂移或本地租约更新失败会按 allocation ID
精确停止对应 RTC，并关闭该逻辑绑定，不按端口或进程名清扫。专项数据库报告 `pg-20260918-164447-29ef5736` 为 10/10，
逻辑会话测试 25/25、Render 能力注入/弱生命周期测试 5/5；`px_render.exe` 与发布目录 SHA-256 已核对。
这些证据仍不等于公网浏览器持续 30 秒以上、撤销、断 Console 和 Android 真机端到端验收。
完整跨平台软件门禁 `pg-20260918-164811-93a8a3ce` 又从三套空 PostgreSQL 重放 Windows/WSL、Web、断库、备份恢复和源码冻结，
在 revision `21c6bd9bf` 上为 735/735 项 PASS；它证明本轮没有破坏软件基线，但仍不替代上述公网与真机 DB5 验收。
