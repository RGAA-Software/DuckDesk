# PostgreSQL 节点控制契约

2026-09-17。这是 Console repository 增量；已加入数据库侧实例对账协议，尚未接入真实 Service 长连接/执行器，不能据此宣称 DB2/DB5 完成。

## 身份与连接

- 设备目录 ID、节点 ID、节点长期凭据、单次连接凭据分开；数据库只保存凭据 SHA-256。设备 enrollment 凭据不能作为节点登录凭据。
- 节点绑定一个设备；一个设备只拥有一个节点身份，产品为 cloud_node 或 remote。设备禁用/删除和节点禁用/删除均阻止连接。
- 节点管理仅接受当前有效的 admin_web 会话；admin 可写，viewer 只读。Android/Panel 用户会话和节点凭据不能调用管理操作。
- `open_connection` 只能由可信长连接入口调用；入口生成新的随机连接凭据，返回的 NodeConnection 留在服务端，不接受客户端反序列化上下文。
- 每次连接递增 generation；报告、断开及后续回执必须携带服务器保存的连接凭据、generation、control_epoch。旧连接不能关闭或覆盖新连接。
- 管理配置使用 revision CAS；重复相同配置不改版本。排空和减少容量不会切断控制连接或终止已有实例；后续启动事务必须检查它们。
- 设备禁用状态变化/删除会在同一事务递增节点 generation 并清除连接；重新启用不会恢复旧连接。设备目录凭据与节点凭据独立轮换。
- 节点长期凭据轮换同时清除连接并递增 generation；前后凭据不并行有效。

## 进程重启与观测

首版支持一个活动 Console，不声明多副本高可用。启动根在对外开放之前执行 `begin_runtime`：

1. 获得管理事务排他 gate，原子递增控制 epoch 并记录进程 UUID。
2. 清除持久化连接，置 offline，递增连接代际；历史端点仅作为历史数据保留。
3. 提交后才能接收节点重新认证。新连接只能进入 reconciling，不能继承旧 ready 快照。

节点报告 sequence 在单连接代际内严格递增。版本号和实际 public_host/端口由报告提供，不接受 URI、路径或隐含端口，
不采用固定旧端口兜底。桌面端口不得落入应用端口区间。端点变化递增 endpoint_revision，端点或能力变化置 reconciling。

`last_seen` 使用数据库时间，不相信节点时钟；`fresh` 表示当前代际最近 30 秒内有已认证联系，不表示容量可用。
ready 还需要完整实例清单 challenge 对账，且启动事务另查部署准备版本和资源占用；普通心跳不能直接变成 ready。
具体见[实例与命令契约](postgresql_instance_contract.md)。30 秒以上心跳间隔后的新报告、端点或能力变化均重新进入 reconciling，清除旧 challenge。
未接通长连接和节点端命令去重前，数据库 epoch 检查不能冒充端到端 fencing。

## 原子性与证据

管理变更与 node_audit 同事务；只允许 runtime 追加审计，不允许更新/删除审计。运行 epoch 写入失败不得清除现有连接。
共享/独占 gate 遵循身份契约，数据库事务内不等待网络响应。

`storage/tests/nodes.rs` 覆盖七组真实 PostgreSQL 用例：角色/凭据隔离、重连/乱序、进程代际、20 路配置 CAS、
禁用/启用/软删除、数据库时钟/设备撤销、审计与 epoch 故障回滚。Windows/Linux 使用同一实现；
节点四张表纳入三库备份恢复的逐行摘要和约束/索引对比。实际运行结果见[实施状态](server_database_execution_status.md)。
