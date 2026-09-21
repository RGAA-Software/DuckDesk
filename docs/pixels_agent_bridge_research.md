# Pixels Agent Bridge 调研

> 当前状态：**第一轮代码级调研完成**
>
> 建档状态：**调研中**
>
> 状态记录：2026-09-21 建档并标记“调研中”；同日完成参考仓库克隆、逐项代码分析、Pixels 边界对照和第一轮结论。
>
> 开始日期：2026-09-21
>
> 范围：仅进行方案与开源项目研究，不修改现有产品实现。
>
> 结论状态：第一轮结论已形成；不等同于已实现、已完成安全评审或已获得第三方代码复用许可。
> 后续讨论：新增第 17–19 节，补充三个重点项目的产品解释、安全边界修正及开源规划讨论稿；仓库拆分、许可证和商业范围均未定案。

## 1. 调研问题

Pixels 当前的远程桌面主要面向人类操作者。本调研评估以下方向：

- AI Agent 运行在用户本机，由本地模型或云端模型负责推理和编排；
- 远端设备不运行大模型，只运行 Pixels 已安装的受控端服务；
- Pixels 为本地 Agent 提供设备发现、身份认证、加密通道、结构化命令、文件访问、交互终端、任务状态、审计与可选隔离环境；
- Agent 执行过程中，用户可以通过现有远程桌面观察、确认或人工接管；
- 保持私有化部署边界，不把客户模型密钥、上下文或远端凭据交给 Pixels SaaS。

## 2. 当前初步判断

该方向在技术上可行。准确的产品形态不应是简单增加一个“远程 CMD”，而应是独立的 **Pixels Agent Bridge** 或 **Pixels Agent Channel**：

```text
本地 AI Agent
    -> 本机 MCP / CLI / SDK 适配器
    -> Pixels Client 自动化会话
    -> Direct 直连或 Pixels Relay
    -> 远端 Pixels Remote Executor
    -> 受控命令、PTY、文件、进程及可选沙箱
```

远端仍然必须存在一个可信执行端点。该端点可以由现有 Pixels 受控端扩展，不需要再部署一个负责推理的大模型 Agent；如果远端既没有 Pixels 服务，也没有 SSH、WinRM 等系统服务，本地 Agent 无法凭空执行远端操作。

## 3. 初步产品边界

### 3.1 本地负责

- 大模型推理、上下文和任务规划；
- 用户交互和高风险操作确认；
- MCP、CLI 或 SDK 工具调用；
- 模型 API Key 与本地凭据管理。

### 3.2 Pixels 控制面负责

- 用户、设备与本地 Agent 身份验证；
- 通过认证连接与服务端 SessionGrant，维护绑定设备、权限、有效期和撤销状态的能力授权；
- 会话准入、撤销、策略和审计索引；
- 不直接接收或执行任意 Shell 字符串。

### 3.3 Pixels 数据面与远端执行端负责

- 建立端到端加密的 Direct 或 Relay 自动化通道；
- 运行结构化命令并流式返回 stdout、stderr 和退出状态；
- 提供受限文件、进程、PTY 和可选端口转发能力；
- 强制执行目录范围、运行身份、超时、资源配额和审批结果；
- 生成可审计、可取消、可重连的任务生命周期。

## 4. 初步工具接口

第一版候选接口如下，名称尚未冻结：

```text
pixels.list_devices
pixels.open_session
pixels.system.info
pixels.process.run
pixels.process.status
pixels.process.cancel
pixels.process.list
pixels.files.list
pixels.files.read
pixels.files.write
pixels.files.patch
pixels.files.upload
pixels.files.download
pixels.terminal.open
pixels.terminal.write
pixels.terminal.read
pixels.terminal.resize
```

桌面截图、键鼠控制、管理员执行和端口转发属于更高风险能力，是否进入第一版需在参考项目分析和威胁建模后决定。

## 5. 与现有 Pixels 设计的关系

现有设计明确要求 Console 管理面不提供通用远程 Shell，并要求部署任务类型化。这一边界应保留：

- `docs/cloud_application_scheduling_plan.md`：Console / Cloud Runtime 不直接执行任意 Shell；
- `docs/service_operations_console_plan.md`：管理界面不提供通用远程 Shell；
- `docs/postgresql_domain_contract.md`：当前节点持久命令限定为 start、stop、reconcile，并使用稳定 command ID、租约和节点幂等；
- `docs/server_refactoring_plan.md`：连接平台可以复用，但不同业务生命周期不应混用。

因此，Agent 功能不应直接扩展现有 start/stop/reconcile 管理命令，而应建立独立的自动化会话、能力授权、任务和审计领域。现有命令的稳定 ID、租约、幂等和代际隔离模式可以作为设计参考。

## 6. 参考仓库与固定版本

| 项目 | 分支 | 固定提交 | 提交时间 | 许可证判断 | 状态 |
|---|---|---|---|---|---|
| [Coder](https://github.com/coder/coder) | `main` | `53ad29512def2e562f41fc17509508896980605e` | 2026-09-20 | AGPL-3.0，另含企业许可 | 已分析 |
| [E2B Runtime](https://github.com/e2b-dev/runtime) | `main` | `112a36052721b254ec09ebcb6c97244d4be35893` | 2026-09-20 | Apache-2.0 | 已分析 |
| [MeshAgent](https://github.com/Ylianst/MeshAgent) | `master` | `b82cbde6136e699ba240cd8f38220ee3264da337` | 2026-09-20 | README 声明 Apache-2.0 | 已分析 |
| [MeshCentral](https://github.com/Ylianst/MeshCentral) | `master` | `c146225fdd674fb60ddfbb2406382c12ad65ee9c` | 2026-09-20 | Apache-2.0 | 已分析 |
| [ShellHub](https://github.com/shellhub-io/shellhub) | `master` | `7b12e729265afa7f6f5e3c0ff3358b1811158493` | 2026-09-19 | Apache-2.0 | 已分析 |
| [ssh-mcp](https://github.com/slepp/ssh-mcp) | `main` | `28834a85d10b59d1e892c5ab4ba34b21d6a80c5c` | 2026-07-08 | MIT | 已分析 |
| [Teleport](https://github.com/gravitational/teleport) | `master` | `1283425b60ec5f60d509ba4c791183d452923ff7` | 2026-08-25 | AGPL-3.0 | 已分析 |

参考仓库统一作为只读研究材料存放在 `D:\source\pixels-agent-research`，不加入 GammaRayPremium 工作树。许可证列只说明仓库当前声明，不构成法律意见；任何代码复制、链接、分发或派生实现都必须另做许可证和安全审查。

## 7. 逐项分析维度

每个项目至少检查以下方面：

1. 远端组件如何注册、保持在线并穿越 NAT；
2. 控制面和数据面是否分离；
3. 命令采用结构化参数还是任意 Shell；
4. stdout、stderr、PTY、取消、超时和断线重连语义；
5. 文件读取、编辑、上传和下载接口；
6. 身份、短期授权、RBAC、审批和远端用户同意；
7. 审计日志、终端录像、输出脱敏与秘密处理；
8. 进程、文件系统、网络和资源隔离；
9. Windows、Linux、macOS 支持范围；
10. 开源许可证、可复用边界和项目成熟度。

## 8. 临时风险清单

- 任意 Shell 会把远控通道提升为远程代码执行平台，必须与普通远控权限分离；
- Windows Service 常以高权限运行，不能把 Agent 命令默认落到 SYSTEM；
- 终端转录和 stdout 可能包含口令、Token 和业务数据，审计与秘密保留策略必须分离；
- 文件编辑需要原子写入、并发修改检测和路径逃逸防护；
- Relay 只能负责传输，不应获得命令明文、文件内容或长期设备凭据；
- Agent 生成代码默认不可信，直接在真实桌面环境执行存在供应链和破坏风险；
- GUI 自动操作比结构化工具更难授权、重放和审计，应后置；
- 本地 MCP Server 自身也是高权限入口，必须限制监听地址、调用方身份和会话范围。

## 9. 调研执行结果

1. 已克隆并固定全部七个参考仓库版本；
2. 已完成连接、命令、文件、PTY、MCP、权限、审计与沙箱的代码级分析；
3. 已对照 Pixels 当前 Auth、Console、Broker/Relay、Client 与 Service 边界；
4. 已形成能力矩阵、可借鉴模式、不可采用模式和最小可行范围；
5. 最终结论见第 16 节。

## 10. 逐项代码分析

### 10.1 MeshCentral / MeshAgent

重点文件：`meshagent/docs/Architecture.md`、`meshcentral/meshrelay.js`、`meshcentral/webserver.js`。

实现观察：

- MeshAgent 从设备侧主动通过 WebSocket/TLS 连接服务器，适合 NAT 后设备和不开放入站端口的环境。
- Agent 使用根 RSA 证书建立身份，公钥 SHA-384 哈希成为 Agent ID，并结合二次认证和服务器证书固定。
- 建立会话时，服务器向浏览器和 Agent 下发唯一 Relay URL/令牌；Relay 建立后还会尝试切换到 WebRTC data-only 直连，WebSocket 保留用于会话终止。
- 权限并非笼统的“可远控”。`meshrelay.js` 分别具有禁止 Terminal、禁止 Files 的权限位；桌面、终端和文件也有独立的通知、提示、隐私条与自动接受策略。
- Relay 支持将会话写入 `.mcrec`。这验证了传输和审计可以关联，但也意味着普通 Relay 模式下服务器能够看到会话内容。
- MeshAgent 内嵌 Duktape，并允许远程替换 Agent Core。该机制扩展性很强，但可以启动子进程和调用本地能力，权限面过大。

可借鉴：常驻端点主动连接、断线恢复、直连优先和 Relay 回退；桌面/终端/文件分权；各敏感能力独立的远端提示与同意；稳定设备身份。

不建议照搬：服务器可读命令和文件内容的普通中继；远程替换高权限脚本 Core；把人类交互终端直接当作 Agent 任务协议。Agent 任务还需要稳定任务 ID、幂等、超时、取消、范围约束和结构化结果。

### 10.2 ShellHub

重点文件：`shellhub/agent/pkg/tunnel/tunnel.go`、`shellhub/pkg/api/client/reverser.go`、`shellhub/server/ssh/server/server.go`、`shellhub/server/ssh/session/events.go`、`shellhub/pkg/models/namespace.go`。

实现观察：

- 设备 Agent 主动建立经过认证的反向 WebSocket，服务器在该连接上复用多个逻辑流。
- `TunnelV2` 按协商协议分发流，每个流独立处理，并具有监听和重连语义。
- 公网 SSH 入口先验证目标设备，再进行计费、Firewall、审批等准入评估，通过后才创建远端隧道。
- 支持 SSH `session` 和 `direct-tcpip`，因此获得 Shell、SCP/SFTP 和端口转发能力。
- Identity 模式支持浏览器即时审批和超时；会话事件有序缓冲并显式处理不完整录像。
- 产品核心仍是 Linux/嵌入式设备的集中 SSH，Windows 不是同等成熟的主要目标。

可借鉴：一条反向连接承载多个逻辑任务流；数据通道打开前完成授权与审批；会话事件有序记录；SSH 可以作为可选兼容后端。

不建议照搬：SSH 是宽权限 Shell 边界，不能自然表达逐工具、逐目录和逐可执行程序权限；Linux SSH 语义也不能作为 Windows 桌面执行的统一抽象。

### 10.3 ssh-mcp

重点文件：`ssh-mcp/src/ssh_mcp/ssh.py`、`ssh-mcp/src/ssh_mcp` 下的工具实现及 `README.md`。

实现观察：

- 它作为本地 stdio MCP Server 被 AI 客户端拉起，不需要开放本机网络端口。
- 工具覆盖一次性命令、持久 PTY、SCP/rsync、远端文件查看/创建/编辑/grep/glob、端口转发以及会话管理。
- 持久会话使用稳定 `session_name`，`ensure` 语义使创建幂等，并可跨工具调用或上下文重置恢复。
- SSH 子进程使用 argv 列表而非 `shell=True`；通用参数拒绝危险 SSH 参数，端口转发被拆成独立工具。
- 输出缓冲有硬上限，但可选 transcript 会保留完整输出；tmux 可让人类旁观 Agent 会话。
- 文件编辑主要是读取、修改、写回，没有强原子性和并发版本前置条件；远端命令仍是任意字符串，真正安全边界是 SSH 账户。

可借鉴：MCP 作为本地入口；命令、文件、PTY、转发拆分为不同工具；稳定会话名、跨调用恢复和有界输出；允许人类旁观。

不建议作为核心依赖：项目规模较小，缺少企业控制面、细粒度授权和跨平台执行层；POSIX 假设较多；任意 Shell 和非原子文件编辑不能直接成为 Pixels 契约。

### 10.4 Teleport

重点文件：`teleport/rfd/0209-mcp-access.md`、`teleport/lib/srv/mcp/server.go`、`teleport/lib/srv/mcp/audit.go`。

实现观察：

- Teleport 把 MCP Server 纳入零信任访问体系，使用短期应用证书和专用 ALPN 连接远端 MCP。
- 角色策略可按 MCP Server 标签和具体工具名授权；deny 优先于 allow，既过滤 `tools/list`，也在 `tools/call` 转发前阻断。
- 支持 JIT 访问、短期身份、RBAC/ABAC、会话开始/结束和请求审计。
- 每个获准客户端可按配置用户启动独立 MCP 进程；结束时先 SIGINT，超时再强制终止。
- 审计区分高价值调用和低信号发现请求，错误结果也会记录。

可借鉴：工具级授权、deny 优先、列表过滤和调用前拦截；短期身份、JIT 审批和目标标签；审计“谁在何时对哪台设备调用哪类工具以及结果”。

不建议照搬：Teleport 面向已注册 MCP/SSH/Kubernetes 等资源，不是任意桌面的本地执行 Agent；它是治理网关而非执行沙箱；仓库体量和 AGPL 许可证也不适合直接嵌入 Pixels。

### 10.5 E2B Runtime

重点文件：`e2b-runtime/docs/ARCHITECTURE.md`、`e2b-runtime/packages/envd/spec/process/process.proto`、`e2b-runtime/packages/envd/spec/filesystem/filesystem.proto`。

实现观察：

- 每个 Sandbox 运行在 Firecracker microVM 中，支持快照恢复、写时复制磁盘、cgroup、network namespace 和 nftables 出站控制。
- 控制面决定放置和生命周期，节点 Orchestrator 负责实际执行；Sandbox 数据流不经过 API 控制面。
- VM 内 `envd` 提供结构化 Process Service：启动、列表、连接、stdout/stderr、stdin、信号和 PTY。
- Filesystem Service 提供 stat/list/make/move/remove/watch、上传和下载，并有端口扫描与转发能力。
- 工作负载身份使用短期 Token，秘密值不经过控制面；进程可在组件升级后被重新接管。

可借鉴：结构化 Process/PTY/Filesystem API；控制面不承载命令输出和文件正文；短期身份、流式输出、进程重连和显式信号；后期隔离运行模式。

不建议第一版照搬：Firecracker 依赖 Linux/KVM，无法覆盖 Pixels 首要的 Windows 场景；每任务 microVM 对普通远控过重。应先实现 Windows/Linux 主机受控执行器，再将沙箱作为可选企业能力。

### 10.6 Coder

重点文件：`coder/docs/admin/infrastructure/architecture.md`、`coder/docs/ai-coder/mcp-server.md`、`coder/codersdk/toolsdk/bash.go`、`coder/codersdk/toolsdk/toolsdk.go`、`coder/agent/x/agentmcp/manager.go`。

实现观察：

- Coder 是本次调研中与目标最接近的完整系统。Workspace Agent 提供 SSH、端口转发、状态和启动自动化，用户通过 WireGuard/Tailnet 与直连或 DERP Relay 访问。
- 同时提供本地 stdio MCP 和带 OAuth2 的远程 HTTP MCP。
- MCP 工具覆盖工作区管理、远端命令、文件读/写/编辑、端口转发、日志和 Agent 对话。
- `coder_workspace_bash` 通过工作区 SSH 执行命令，支持自动启动工作区、最大超时和后台执行；文件操作被引导到专用工具，而不是全塞进 Bash。
- Workspace Agent 还能发现 `.mcp.json` 中的 MCP Server，支持 stdio/HTTP/SSE、超时、工具名前缀、文件监听重载和受限子进程执行。
- Windows 有 Agent 和 ConPTY 相关实现，但部分工作区能力仍有平台差异。

可借鉴：“本地 MCP → 控制面授权 → 远端 Agent → 命令/文件/端口工具”的完整工作流；Agent 与直连/Relay 底座组合；文件工具与 Shell 分离；Agent 会话和人类工作区体验结合。

不建议照搬：Coder 的核心对象是受管开发工作区，不是客户已有的任意桌面；命令工具仍接受 Bash 字符串；工作区模板和生命周期不能替代 Pixels 当前业务生命周期；AGPL 与企业许可拆分要求仅作架构研究。

## 11. 能力对比矩阵

`部分` 表示存在相近能力但边界或平台不同；“Pixels 建议”是拟议目标，不代表已实现。

| 能力 | MeshCentral | ShellHub | ssh-mcp | Teleport | E2B | Coder | Pixels 建议 |
|---|---|---|---|---|---|---|---|
| NAT 后端点主动连接 | 是 | 是 | 依赖 SSH 可达 | 是 | 不适用 | 是 | 是 |
| Direct / Relay 回退 | 是 | Relay 为主 | 依赖 SSH | 是 | 不适用 | 是 | 是 |
| 本地 MCP | 否 | 否 | 是 | 是 | 否 | 是 | 是，首发入口 |
| 结构化进程 API | 否 | 部分，SSH exec | 否，命令字符串 | 非重点 | 是 | 部分，Bash/SSH | 是，默认 argv |
| 持久 PTY | 是 | 是 | 是 | SSH 域有 | 是 | 是 | 后续高风险能力 |
| 文件 API | 是 | SCP/SFTP | 是 | 依赖目标 | 是 | 是 | 是，版本化 |
| 工具级 RBAC | 协议级分权 | 部分 | 依赖客户端 | 是 | 部分 | 部分 | 是，deny 优先 |
| JIT / 远端同意 | 是 | 是 | 否 | 是 | 否 | 部分 | 是 |
| 审计 / 记录 | 是 | 是 | 本地 transcript | 是 | 可观测性 | 是 | 是，内容留存分离 |
| 强隔离沙箱 | 否 | 否 | 否 | 否 | microVM | 取决于模板 | 后续可选 |
| Windows 主要目标 | 是 | 否 | 弱 | 部分 | 否 | 部分 | 是 |
| Linux 主要目标 | 是 | 是 | 是 | 是 | 是 | 是 | 是 |

## 12. 与 Pixels 当前架构的边界核对

可以复用：

- Auth/Console 的身份、设备归属、许可证和授权关系；
- Broker 的在线端点、路径协商和 SessionGrant 思路；
- Direct 优先、Relay 回退的连接底座；
- 节点命令已有的 `command_id`、`lease_id`、deadline、代际校验、至少一次投递和幂等原则；
- `px_service` 的常驻身份、断线重连、本地持久回执和生命周期监督；
- 远程桌面作为观察、确认和人工接管通道。

必须保持独立：

- `docs/cloud_application_scheduling_plan.md:24` 明确 Console / Cloud Runtime 不直接执行任意 Shell；
- 同文档 `:181` 要求部署只接受类型化任务，不接受页面传入任意 Shell/路径；
- `docs/service_operations_console_plan.md:12` 明确管理界面不提供通用远程 Shell；
- 同文档 `:33-35` 将部署执行器限定为绑定服务、版本、任务和代际的受限任务；
- `docs/postgresql_console_runtime_contract.md:138` 禁止公开任意文件路径下载或任意 Shell 管理接口；
- `docs/postgresql_domain_contract.md:27` 将节点持久命令限定为 start/stop/reconcile，并通过 command ID 去重；
- `docs/server_refactoring_plan.md:133-136` 要求业务与连接分离、共用连接底座但不共用业务生命周期，Relay 是纯数据面；
- 同文档 `:975-978` 再次确认各业务域复用 Broker/Relay，但保持独立生命周期与稳定 Session 身份。

所以不能把 Agent 命令塞入 Console 运维网页、云应用 start/stop/reconcile、服务部署执行器、Relay 业务逻辑或 Render 媒体控制协议。应新增独立的 **Automation Session Domain**，具有独立会话类型、能力集合、任务 ID、执行身份、审计事件、取消和结束语义。

## 13. 建议的协议和安全模型

### 13.1 授权

每个自动化会话使用有有效期、可续期和可撤销的能力授权，至少绑定 deployment、用户、本地 Agent 客户端实例、目标设备、允许工具、路径范围、允许程序、网络目标、执行身份、有效期、任务/资源上限、远端同意要求、会话 ID、授权 revision 和撤销状态。集成 Pixels 时遵守现有认证连接与服务端 SessionGrant 契约，不重新引入要求客户端数秒内消费的一次性建连票据。这里的“短期”是权限时效，不是另一套连接身份体系。

采用 deny 优先策略。`tools/list` 只列本会话可用工具，`tools/call` 在本地适配器和远端执行器重复校验关键约束。

### 13.2 执行身份

1. **桌面用户模式**：默认，在当前获授权的交互用户身份下执行。
2. **提权模式**：单独能力、单独审批、短时有效；绝不能因为 `px_service` 是系统服务就默认使用 SYSTEM。
3. **沙箱模式**：后续能力，用于不可信代码、构建和批处理；Windows 与 Linux 分别选择合适隔离技术。

### 13.3 结构化进程

第一版不把完整 Shell 字符串作为默认接口：

```text
ProcessStart {
    task_id,
    executable,
    argv[],
    working_directory,
    environment_references[],
    stdin_mode,
    timeout,
    output_limit,
    execution_identity,
    capability_revision
}
```

Shell 和 PTY 属于更高风险能力。长任务需要稳定 `task_id`、幂等创建、stdout/stderr 分流、递增 chunk sequence、有界缓冲、从 offset 恢复、status/cancel/signal、最终退出状态和明确的授权到期策略。

结构化 argv 只能减少拼接与解析歧义，不能提供操作系统隔离。允许 Python、PowerShell、编译器或可加载插件的程序，仍可能获得广泛文件和网络访问能力。文件工具的目录限制不自动约束这些进程；必须根据实际 OS 身份与隔离方式说明能力边界，不把 cwd 当作沙箱。

### 13.4 文件

- 路径在远端规范化后检查允许根目录，拒绝路径穿越、链接逃逸和设备路径；
- `write/patch` 携带预期版本或哈希，发现并发修改时拒绝覆盖；
- 临时写入、校验后原子替换；
- 上传/下载使用分块、摘要、大小限制、取消和断点状态；
- list/read/write/delete/move 分开授权；
- 默认禁止读取浏览器密钥、系统凭据、Pixels 私钥和受保护配置。

### 13.5 数据面与审计

- 命令、输出、文件正文在 `px_client` 与远端 `px_service` 之间端到端加密；
- Broker 只做准入和路径协调，Relay 只转发密文；
- Console 仅持久化授权、任务元数据和审计索引，不进入高带宽输出路径；
- 审计默认记录调用者、目标、工具、范围、时间、结果、字节数和批准者；
- 完整 stdout/stderr、终端录像和文件内容是独立合规选项，不能被“开启审计”隐式启用；
- 输出截断、秘密脱敏和保留期必须可配置。

### 13.6 本地 MCP

- 首选 stdio MCP，由 AI 客户端拉起，避免默认监听本机 TCP；
- HTTP MCP 如有需要只监听 loopback，并加入一次性会话身份、防跨站请求和调用方绑定；
- MCP Server 不保存设备长期密钥，只向 `px_client` 请求短期会话；
- 高风险确认由 Pixels UI 完成，不能只相信模型生成的确认文字；
- MCP 是第一适配层，但内部使用稳定的类型化 Automation API，以便后续提供 CLI/SDK。

## 14. 能力分层与首版范围

| 能力包 | 示例 | 风险级别 | 首版建议 |
|---|---|---|---|
| `system.read` | 系统、磁盘、进程、服务状态 | 低到中 | 提供 |
| `files.read` | 列目录、读文件、搜索 | 中 | 受限提供 |
| `files.write` | 创建、补丁、移动、上传 | 高 | 受限提供 |
| `process.run` | 结构化进程、状态、取消 | 高 | 受限提供 |
| `terminal.pty` | 交互 Shell、stdin、resize | 很高 | 后置 |
| `network.forward` | 本地/远端端口转发 | 很高 | 后置 |
| `desktop.observe` | 截图、窗口信息 | 高，含隐私 | 后置 |
| `desktop.control` | 鼠标、键盘、剪贴板 | 很高 | 后置 |
| `privilege.elevated` | 管理员/root/SYSTEM 边界 | 极高 | 后置 |

第一版建议只做 `system.read`、受限 `files.read/write` 和结构化 `process.run`，并先验证 Windows。PTY、端口转发、桌面控制和提权不随基础能力默认开放。

## 15. 明确不采用的方案与后续阶段

不采用：

1. 只给 Agent 一个远程 CMD；
2. 在远端再运行一个大模型 Agent；
3. 把命令放进 Console 运维 API；
4. 把自动化数据混进桌面媒体协议；
5. 让 Relay 解密命令或文件内容；
6. 默认以服务账户/SYSTEM 执行；
7. 第一版强制 microVM；
8. 第一版依赖 GUI 视觉操作；
9. 直接嵌入 Coder 或 Teleport。

若立项，建议分阶段推进：

1. 威胁模型、加密终点、Automation Session/Capability/Task 状态机；
2. 本地 stdio MCP、只读系统工具、短期会话和基本审计；
3. 结构化一次性进程、流式输出、取消和断线恢复；
4. 带版本前置条件的原子文件服务；
5. 长任务与 PTY；
6. JIT 提权和企业策略；
7. 可选沙箱；
8. 最后增加截图、窗口和输入等 GUI Agent 能力。

## 16. 最终结论

### 16.1 是否值得做

值得。传统远控解决“人如何操作远端桌面”，Agent Bridge 解决“本地智能如何在授权范围内可靠操作远端计算机”。两者结合后，Pixels 可以从远控工具扩展为通用的私有远程计算入口，而不必自己运营模型 SaaS。

### 16.2 是否已有完全相同的开源方案

没有发现一个项目同时满足：面向现有跨平台桌面、本地 MCP、Pixels 式 Direct/Relay、端到端加密、工具级授权、Windows 用户身份执行、版本化文件编辑、任务恢复和可选沙箱。

Coder 已证明端到端体验成立，但面向开发工作区；其余项目分别证明端点连接、治理、MCP、SSH 和沙箱成立。Pixels 的机会是把这些经过验证的模式组合到现有远控和私有化部署边界中。

### 16.3 推荐产品定义

> **Pixels Agent Bridge：让用户本机的任意 AI Agent，在不托管模型和凭据的前提下，通过 Pixels 的身份、授权和加密连接，安全地操作已授权远端设备。**

核心卖点：模型无关、私有化部署、Direct 优先/Relay 回退、结构化命令和文件工具、工具级短期授权、远端同意、可撤销，以及通过远程桌面观察和接管。

### 16.4 建议冻结的架构方向

1. 新建独立 Automation Session Domain，不扩展 Console 通用 Shell；
2. `px_service` 是远端可信执行器，不内嵌模型；
3. MCP 是第一入口，但内部协议保持独立和类型化；
4. Direct/Relay 复用连接平台，正文端到端加密；
5. 结构化进程和版本化文件服务优先，PTY/提权/GUI 后置；
6. 默认以授权桌面用户执行，提权必须独立授权；
7. 审计元数据与完整内容留存分离；
8. 不整体引入被调研项目；任何局部代码复用另做法律和供应链审查。

本轮尚未完成、进入实现前必须另立文档处理的事项包括：正式威胁模型与密码协议评审；Windows 用户令牌、UAC、Session 0 专项验证；macOS 权限验证；产品授权与企业策略；协议、数据模型、测试矩阵和实施计划。它们不影响“方向可行、架构应独立”的第一轮结论，但完成前不能宣称功能已实现或达到生产安全标准。

## 17. 三个重点项目的产品解释补充

### 17.1 Coder：持续存在的远端工作环境

Coder 以工作区为核心，通过模板组织虚拟机、容器、工具和存储。Workspace Agent 是工作区内的连接与执行服务，不等于进行推理的 AI Agent。外部 AI 客户端通过 MCP 可以查询环境、读取和编辑文件、执行命令及访问结果。

例如用户要求“在开发工作区运行测试并修复失败”，AI 可以选择工作区、读取测试文件、执行测试、修改文件、重新验证。价值在于环境身份、文件、命令和任务结果围绕同一持续存在的工作区组织。

已查看版本的本地 MCP 命令与远程 HTTP 入口带实验性标记，不应把已出现的能力一概认定为稳定发布契约。参考 `coder/docs/ai-coder/mcp-server.md`。

Pixels 面对的通常是客户已有桌面，可能包含未保存文件和运行中的软件。可借鉴工作环境与工具组织方式，但不套用开发工作区的重建、销毁、闲置停机策略，也不必建设 Terraform 模板平台或模型网关。

### 17.2 ssh-mcp：本机 Agent 调用远端能力

典型链路为“本机 AI 客户端 → 本机 stdio MCP → 本机 OpenSSH → 远端 SSH 服务”。远端无需第二套大模型，但必须具有可达且获授权的执行服务。

它分别提供一次性命令、持久 PTY 和文件工具：一次性命令返回输出及退出码；PTY 保留当前目录、环境和终端状态；文件工具避免 Agent 每次自行拼接文本处理命令。会话名帮助跨工具调用找回终端，但不意味着服务重启或任意网络中断后自动恢复一切状态。

Pixels 可以采用同类工具体验，把 SSH 后端扩展为自己的设备连接与执行服务。它本身不解决设备注册、跨 NAT、企业管理和完整权限治理。MCP 工具分开也不等于权限已隔离：任意 Shell 可以绕过文件工具，通过自身权限直接访问文件。

### 17.3 MeshCentral：已有设备的接入和远程管理

MeshAgent 安装在已有机器上并主动连接服务器；管理员通过 MeshCentral 网页访问桌面、终端和文件。设备在线状态与实际操作会话分离，因此自动化命令不需要先开启视频流。

可借鉴桌面、终端和文件的独立权限、远端同意、会话指示与记录。其传统终端面向人类，Agent 还需要任务 ID、运行状态、输出序列、取消和恢复协议。普通 WebSocket Relay 模式由服务器终止两端 TLS，不能据此承诺中继不可见明文。

### 17.4 数据隐私表述修正

“Pixels 不托管模型密钥或上下文”仅约束 Pixels 自己的系统。若本地 AI 客户端调用云端模型，读到的远端文件、日志或截图仍可能随上下文发给该供应商。私有部署不自动等于数据完全不出网，实际边界取决于客户选择的 Agent、模型和工具策略。

## 18. 开源项目规划讨论稿

> 状态：讨论中。用户提出将 Agent 能力开源以吸引用户，并希望评估独立项目与公共部分复用。以下为建议，尚未授权创建或公开仓库、搬迁代码、更改许可证或商业套餐。

### 18.1 推荐定位

建议把 Agent Bridge 做成可独立使用的开源产品，同时作为 Pixels 的执行能力来源。首要用户是希望让本机 Agent 操作自己另一台 Windows/Linux 机器的开发者、技术用户和小团队。

首发场景可以聚焦“本机 Agent 操作远端 Windows 项目：读取日志、编辑文件、运行构建并取回结果”。它利用已有桌面环境，不要求用户先迁移到新工作区或部署整套 Pixels。

不应只开源一个必须购买 Pixels 才能使用的 MCP 壳。社区版本应独立下载、构建、配对、执行和读取结果，不依赖 Pixels 账号、商业许可证、私有包仓库、Console 或官方在线验证。与此同时，不扩成另一套完整远控、设备管理和云计算平台。

### 18.2 三种组织方式的取舍

| 方式 | 优点 | 当前代价 | 建议 |
|---|---|---|---|
| 只在现有私有仓库增加 MCP，之后公开适配器 | 初期改动少 | 社区缺少可独立使用产品，容易暴露私有耦合 | 作为内部验证可以，不作为主要开源定位 |
| 从 Pixels 大规模抽取网络、身份、日志、数据库等公共平台 | 理论复用广 | 当前契约仍变动，重构会与主产品相互阻塞 | 暂不做 |
| 新建小型开源项目，包含独立入口与可复用执行核心 | 独立价值明确，Pixels 单向依赖 | 需要维护明确边界和版本 | 推荐 |

推荐未来使用一个独立公开仓库管理开源产品及其核心库。无需另开第三个“公共基础平台”仓库，也不应公开当前私有仓库的整个历史。当前阶段仅讨论和设计，尚不执行仓库创建或代码搬迁。

### 18.3 单一实现与依赖方向

开源仓库建议包含：

- 协议：能力声明、任务状态、输出和文件操作契约；
- 执行核心：进程、文件、取消、超时、输出管理和本地策略；
- 平台实现：Windows/Linux 的身份、进程和文件适配；
- 独立执行程序：配置、配对、监听、关闭；
- 本地 MCP/CLI：面向 AI 客户端和人工诊断；
- 基本认证传输、示例、文档与契约测试。

这些是职责模块，不要求每项独立进程、仓库或包。结合当前 Rust Service，可优先评估 Rust 核心与独立二进制；语言和最终进程边界尚未冻结。

Pixels 私有仓库通过固定版本依赖开源核心，提供 Console 身份、连接授权、Broker/Relay、远程桌面 UI 与企业管理集成。集成时可优先考虑带受限本机 IPC 的执行子进程，以表达不同用户身份和故障边界；是否同进程嵌入应在具体设计时决定。无论部署形式如何，执行核心只能有一个维护来源。

依赖方向为“Pixels 集成 → 开源核心”。开源核心不调用私有 Auth，不导入 Console 数据模型，不依赖 Pixels 媒体协议。权限校验在执行边界实际执行，不能仅靠界面隐藏工具。

Pixels 固定开源发布版本和锁文件，核心变化通过契约测试验证后升级。禁止在两边复制执行代码分别修改，也不跟随上游 main 自动变更生产行为。

### 18.4 首版如何独立联网

为避免重复开发当前连接平台，首个验证版本可使用 LAN 或客户已有 VPN 下可达的端点，配合标准 TLS、明确配对和目标身份验证。不需要公网控制面、账号系统或官方 Relay。

这意味着首版不能承诺“任意网络下一键连接”；两个 NAT 后设备是否可达取决于用户网络。首发差异应落在 Windows 原生执行、安装容易、可靠任务输出和文件编辑，而不能只依靠与 ssh-mcp 相同的命令封装。

等独立执行体验得到验证，再接入 Pixels 既有连接底座。若社区用户大量需要自托管 Relay，应另行评估抽取稳定的传输模块，不预先复制第二套 Broker/Relay。

SSH 可以作为后续可选适配，但首版同时维护 SSH、原生传输和多种平台会扩大验证范围，建议先验证原生 Windows 端到端流程。

### 18.5 社区与商业的建议边界

| 能力 | 开源独立版 | Pixels 商业集成 |
|---|---|---|
| 命令、文件、任务、输出与取消 | 完整可用 | 复用同一实现 |
| 基本身份验证、加密、当地权限、撤销和本地操作记录 | 必须具备 | 复用并接入企业策略 |
| 手工配置多台机器、LAN/已有 VPN 连接 | 可用 | 统一目录和设备发现 |
| 远程桌面观察与人工接管 | 不在首版范围 | 接入 Pixels 现有产品 |
| 组织身份、SSO、集中策略、审批、审计检索 | 不在首版范围 | 候选商业价值 |
| 大规模分发、升级、设备运维和交付支持 | 社区文档与手工管理 | 候选商业价值 |

这是产品组织建议，不修改已经确定的远控价格或服务权益。已有“企业版优先升级、企业 Plus 一对一支持、OEM 另收费”的约定保持独立，不因本讨论自动改变。

不开源基础安全能力的削弱版本；收费理由应是团队管理、完整远控体验、部署运维和服务。社区版应允许真实工作，避免水印、功能性超时和强制账号成为首次体验障碍。

### 18.6 许可证和品牌

若目标优先是传播、易集成和被 Pixels 闭源产品复用，可优先讨论 Apache-2.0。其许可证提供版权与专利授权，并有再分发和声明保留要求；这同时意味着其他厂商也能在遵守条件时使用该核心。参考 [Apache 官方条款](https://www.apache.org/licenses/LICENSE-2.0)。本文仅记录许可证选项，没有给现有代码重新授权。

如果用户不能接受竞争者商业复用，就需要重新讨论许可证与商业目标。禁止商业用途的限制不符合 OSI 开源定义，不能一边作此限制一边按标准开源宣传。参考 [OSI 开源定义](https://opensource.org/osd)。

独立仓库可使用 Pixels 关联命名和清晰的维护者说明，使实际使用者知道完整商业产品，但下载与运行无需注册营销账号。许可证适用范围、品牌使用和第三方来源分别说明。

### 18.7 与未完成 Pixels 的推进顺序

1. 先写清范围与依赖契约，避免开源项目必须等 Console/Broker 全部完成。
2. 用一个本地 AI 客户端、一台 Windows 远端机完成文件读取、修改、构建、输出与取消的闭环；只选择真实存在的扩展边界。
3. 验证执行核心能够被 Pixels 集成，而不拷贝代码或另造任务状态机；可先做窄接口验证，不要求全面接入。
4. 完成独立构建、基本认证、安装说明和同范围测试，发布明确标注实验性质的版本；Linux 在实际验证后再列支持，macOS 后置。
5. 根据用户使用情况接入 Pixels 连接与桌面能力，再决定是否有必要抽取网络公共模块。

如果步骤 2 需要重写一整套 Console、Broker、媒体或数据库才能运行，说明边界仍然过大，应收缩。若首批用户普遍卡在联网而非执行体验，则调整发布顺序，先解决可达性或提前验证稳定连接适配，不把 LAN 限制包装为完整公网方案。

### 18.8 引流效果如何判断

首先验证下载用户能否完成首次远端任务、是否持续使用、是否愿意反馈问题或贡献适配。记录通过自愿反馈、讨论区和可选择的体验反馈获得，不要求秘密采集机器或任务内容。

GitHub Star 只能作为关注指标。更有价值的是首次成功率、重复使用、外部贡献，以及实际提出远控接管、团队身份、集中管理需求的用户。Agent 用户与云游戏/云渲染付费用户不必然重合，商业转化是需要验证的假设。

对外示例建议围绕三种可重复演示：远端 Windows 构建和取回产物、授权目录内批量文件处理、读取日志并运行诊断。确保示例能在独立版真实运行，再展示 Pixels 集成带来的设备发现、联网便利和桌面接管。

## 19. 当前讨论建议

推荐：独立开源仓库、独立可用产品、单一执行核心、Pixels 单向依赖；首版聚焦可达网络中的 Windows 自动化闭环。

暂不推荐：为开源而拆整个 Pixels 公共平台、维护两套执行器、复制私有仓库、首发完整云工作区平台，或把未完成的网络能力承诺为现成产品。

这条路径不要求 Pixels 全部开发完毕才开始，也不要求立刻将 Pixels 大范围开源。下一步讨论的关键是用户是否接受社区版独立商用，以及首版优先验证执行体验、明确保留 LAN/已有 VPN 的联网范围；许可证和商业边界仍待用户决策。
