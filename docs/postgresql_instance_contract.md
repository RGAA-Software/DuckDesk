# PostgreSQL 应用实例与节点命令契约

2026-09-17。本文对应 Console storage，不代表真实 Service 已接通或 DB2/DB5 已验收。实际证据见[实施状态](server_database_execution_status.md)。

## 预约与所有权

实例绑定 application/deployment/node、业务 owner、原始登录或访客会话、client_type、授权版本及不可变启动快照。
Android 为 `android`，不冒充 Panel；资源会话后续必须使用独立 CloudApplication 目标，不能把这里的 node/device 当会话目标兜底。
同主体 request_id 与正文摘要幂等；改变应用、硬选部署或客户端类型不能复用请求 ID。

预约事务在统一控制 gate 内复查当前身份、ACL、节点当前代际及 30 秒新鲜度、Ready、部署准备版本、排空和容量。
同节点活跃端口唯一；同 RDP `(application,node)` 只有一个活跃实例。Unknown/ReconcileRequired 仍占容量，不能因重启或超时释放。
排序仅实现可用槽数量和稳定 ID；并非多 GPU/编码器预算调度的完成证据。

## 命令与回执

实例转换、命令、事件同一事务。先提交再发送网络请求。领取有 15 秒租约和 60 秒命令截止；重领保留 command ID，更新 lease ID。
Start 携带不可变启动快照与精确 launch ID；Stop 只针对精确实例/launch/代际/版本，不能按 PID、端口或路径扫进程。
节点必须持久化命令去重与版本 fence，不能仅依赖 Console 的数据库检查。

回执检查连接、控制 epoch、节点代际、实例、launch、命令版本、租约与实际端口。重复已完成回执只能匹配原结果与租约，
返回当前状态，不回退新状态或重复结束已复用资源。完成回执和补偿 Stop 同事务，任何事件/命令写入失败均回滚。

- 未领取的 Reserved 被撤权、配置失效或超时：可证明从未派发，取消命令并结束预约。
- 已领取、结果未知：保留占用并要求完整清单对账，不能推断“没有运行”。
- 配置变化或排空：禁止新启动；已领取的启动先对账，不强杀已运行工作负载。
- 授权失效：已领取 Start 使用更高版本的精确 Stop；Running 回执和对账也重新检查当前授权，不能恢复已撤销访问。
- 原始会话绑定不能被另一个会话隐式替换。后续显式授权重连须另外建模当前前端资源会话；不要把新会话当成原始启动身份。
- 已停止实例重复 Stop 为成功，无进程/端口清理；管理 Stop 包含同事务追加审计，viewer 拒绝。

## 对账协议

1. Console 验证当前节点连接，取消未决命令，将活跃实例置 ReconcileRequired，提交 30 秒单次 challenge。
2. 节点先持久化每个 launch 的 **inclusive `reject_through_revision`**：之后拒绝所有小于等于此版本的命令，
   并等待此前正在执行的 Start 完成或确认撤销，然后采集完整的产品自有运行时清单。只回传 nonce 不构成 fence 证明。
3. Console 验证 challenge、deadline、连接代际和完整清单；未知实例、错 launch/端口、重复项均拒绝，保持不准入。
4. 精确存在且仍授权的运行时恢复 Starting/Running；已请求停止或已撤权的存在项补发精确 Stop。
   清单确认不存在的项才结束并释放容量；已经请求停止的项记 Stopped，不改 Failed。
5. 单事务消费 challenge 并置 Ready。端点/能力变化、心跳过期、重连或 Console epoch 变化使旧 challenge 失效。
   Ready 不绕过部署准备的 endpoint revision 检查；旧空清单不能结束后来的实例。

未知或外部进程不得收养/清理。GameHook 必须继续遵守私有 Job AND 完整路径；RDP 只结束运行时与传输，
不得注销 Windows 会话、删除账号/Profile 或终止工作区应用。数据库测试没有验证 Windows 操作系统的这些动作。

## 当前验证边界

`tests/instances.rs` 包含两个实际 OS 进程争抢最后一个槽；`tests/commands.rs` 包含并发领取、重领、精确回执、撤权时序、
审计/命令/事件故障回滚、排空/配置变化、重连、完整清单与端点变更。节点为测试适配器，不是部署后的 Windows Service。
工作区凭据与资源会话 repository 已分步实现，证据见各契约和执行状态。尚待接通真实长连接/节点 fence、
工作区受保护交付、权限事件执行器和传输租约硬截止；不能以本契约代替全链路验收。
