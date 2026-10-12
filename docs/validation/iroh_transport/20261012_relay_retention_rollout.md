# Console／节点配套上线与实网维护检查（2026-10-12）

用户明确回复“用90，可以用了”，替代此前暂停使用90的约束。本轮完成最新容量／排空保留策略的配套开发上线。
90 暂存仅在 `D:\112233\relay-retention-rollout-20261012`，XR 暂存仅在 `D:\uploadsoft\rd\pixels\relay-retention-rollout-20261012`。
Windows 20348、SYSTEM/native 执行上下文由 Pixels MCP connect 确认；没有向 Desktop 放文件。

## 发布结果

先读取实际服务进程路径和已安装完整基线，发现真实产品目录为 `C:\Program Files\Pixels Cloud Node`，含空格。
90 原 Service EE2CFCC7…，XR 原 Service636C688E…；90 还有自定义 TOML，不能将新候选的默认配置或清单直接覆盖过去。
按 Console → 90 node → XR node 的顺序，只传变化文件。没有编译、重打整包或重新执行 Setup，属于既有完整安装基线之上的授权增量开发更新。
此前正式安装、两系统 Relay 完整包与构建证据继续复用；不将本次手工增量发布称为新的 Setup 安装验收。

| 更新对象 | 变化的运行文件 | ZIP 字节 | 最终运行 SHA |
|---|---|---:|---|
| 90 Console | `bin/px_console.exe`、新版 JS、`index.html` | 7,220,064 | `C783635BE09091D6BBA21159141AAE1D3F28C7CAAF366749010EDF804441B265` |
| 90 Node | `px_service.exe` | 3,941,437 | `A06AF7FE0E9F0DC7CDFBA78A443874A2855B5DA2F18CA4B770764FBE115671ED` |
| XR Node | `px_service.exe` | 3,941,437 | 同上 |

每包还带对应清单和发布计划／脚本。上传完成 SHA 与源 ZIP 一致，预检通过后才停止对应服务，备份将替换的文件，发布并逐项核对所有运行清单，再启动服务。
Console 的旧生成 JS 先备份再移除，其他 Server 程序与 Relay／Backup 服务未替换或重启。
Server 35 个清单载荷文件＋清单本身核对；每个 Node 314 个载荷文件＋清单本身核对。此前报告的35／314是载荷数，新增脚本返回36／315含清单。

公开 HTTPS 实际服务的首页和新 JS 均为 HTTP200，响应内容 SHA 与部署文件一致，浏览器刷新可读取新版资源。

Server manifest `4B2D26D7…`；90 Node manifest `5943655C…`、XR Node manifest `BBC326BC…`。
90 自定义 `px_service.toml` 的 `55904D03…`、XR 的 `38363D78…` 均与更新前完全一致。
两个节点 Service Running／A06AF7FE…，Console Running／C783635B…，两节点和两个 Relay 的控制上报均 fresh/ready、epoch72。
旧节点在 Console 更新后、各自 Service 更新前发生短暂重认证，generation 从90的453到463、XR的8到20；更新后在多个上报周期中维持同一代际，不称整个上线过程零重连。

## 实网检查

- 使用已安装的90默认 Render 作为既有端点参照；从服务端只读查询其实际上报描述，端点身份前后均为 `bdc8b9e8…`、home仍90，配置保留两侧。
- 低流量 helper 在90 Relay保持一条 QUIC连接／同一可靠流和两端 Endpoint，临时通过现有管理接口将90 Relay排空，并等待真实 reported_draining 生效。原生产连接1＋helper2在90仍为3，未丢弃既有连接。
- 原helper在排空时完成4次可靠往返、每方向4个4字节数据报校验，同一身份／连接／流保持；新尝试90的端点收到明确拒绝。
- 按管理接口报告的可用状态，将两枚新测试端点配置到BJ，实际两个home均BJ，完成同样可靠／数据报收发；BJ实际上报2连接。这不是实际业务客户端的授权或视频验收，也不声称拿到了生产 NodeResponse：真实WS／Service策略组合证据沿用前两批。
- helper正常退出，端点全部关闭，临时排空设置恢复为原false，两Relay fresh/ready、原disabled=false保持。90原生产端点仍1连接，BJ恢复0；它是既有运行资源，不是测试残留。
- 最终分页核对328条资源会话全部closed，无未结束实例摘要、无新增用户／权限或临时应用。90维持正常默认Render，XR没有应用Render，测试probe进程0。

最初的业务描述读取夹具缺主体标头／请求了不支持的桌面observer，并遇到未授予桌面访问的403及过期旧测试账号401；均未创建资源会话或改变权限。
改用现有管理员控制接口和只读端点快照，未为验证扩大用户权限或增加认证机制。其他调用错误（请求UUID／大小写哈希／路径空格／输出预览截断）保留在索引，不算传输失败或通过。

## 证据与接续

[发布身份、传输时长、真实计数和最终状态](20261012_relay_retention_rollout.json)，[helper实际收发日志](20261012_relay_retention_rollout_probe.log)。
XR本次上传3,941,437字节／9.415秒，约0.419MB/s（3.349Mbps）；这是Pixels上传速度，不是Relay吞吐或视频带宽。
发布脚本为 `scripts/server_validation/apply_transport_incremental.ps1`，先支持 VerifyOnly，含路径／重解析点／基线及载荷SHA检查，只处理声明服务和文件；各端旧文件备份保留在暂存目录。
未跑新的媒体、NIC或饱和混传测试，未更改第三方iroh或Android。Company Client FE8B8257…身份沿用，本批未更新它。
本轮配套发布和维护收发检查已收尾；实际用户视频性能不由这些小消息推出。晚间从既有三次151–165ms短顿及反馈证据接续，未设置自动启动时间。
