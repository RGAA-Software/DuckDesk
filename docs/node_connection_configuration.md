# 节点连接配置

Render 安装包在所有节点上保持完全一致，不需要也不允许部署人员为每台机器改配置文件。Panel 的“网络”页面是普通部署的唯一配置入口：

- 粘贴授权信息时取得并保存 Console 地址；
- “节点公网地址”填写客户端能够访问当前 Render 节点的 IP 或域名，可留空用于纯局域网地址上报；
- Panel 通过本机管理通道把两项地址和授权一起交给 Service；Console 首次确认授权后，Service 使用 Windows DPAPI 持久化，并在重启后自行恢复；
- Service 启动 Render 时注入节点地址和端口，不要求 Render 再读取机器专属地址。

Console 可以在独立机器上；节点公网地址必须指向当前 Render 节点，不能填 Console 地址代替。每台节点在自己的 Panel UI 中完成授权，Console 不重复维护同一份地址。

当 UI 中的节点公网地址是 IPv4 时，Service 会把它作为该节点 Web RTC 直连的受控通告地址传给 Render。域名仍可用于 Native 和 Console 路由；Web RTC 的域名解析 / IPv6 通告尚未实施，当前应使用 IPv4 地址，或在独立 Render 的 `[rtc].advertised_ipv4` 明确填写可达 IPv4。

UI 保存后 Panel 会持续下发最新值；新启动的 Render 自动使用它。已运行的 Render 若需要改变 RTC 通告地址，由 Panel 的正常重启 Render 流程生效。空值不表示已经具备公网访问能力。

Panel 网络页只允许编辑授权信息和节点公网地址。授权信息解析出的 Console / Relay 端口，以及安装包实际加载的管理端口、桌面端口、应用端口池、RTC 端口池和 Panel 本地端口均只读展示，用于部署和防火墙核对。连接协议开关、网卡和监听端口不再提供逐机 UI 覆盖。Settings 中不再提供 Mobile Clients 和 Plugins 页面。

## 高级监听配置

`px_service.toml` 仅随安装包携带高级端口默认值，普通部署不编辑。只有制作特殊安装包或默认端口冲突时，才修改源码目录中的统一模板后重新打包；省略配置段即使用代码默认值。

```toml
[network]
# 本机管理接口绑定地址。Panel 与 Render 默认同机连接，保持回环地址即可。
# 不需要公网映射；不要为了让 Console 接入而开放它，Service 会主动连接 Console。
listen_host = "127.0.0.1"
# TCP，本机管理端口；同目录 Panel 会读取此值，Service 会传给它启动的 Render。
listen_port = 4603
# TCP + UDP，桌面 Render 对外连接端口，两种协议需同时放行。
# 分机部署时映射到该渲染机器，而不是 Console；映射端口建议与监听端口一致。
desktop_port = 4601
# TCP，Panel 管理与录像访问端口；客户端访问录像时使用，需在节点公网同号映射。
# 该端口从应用池尾部独立划出，不能与应用实例端口重叠。
panel_port = 4999
# UDP，供本机 Panel 发现局域网 Console，必须与 Console 的发现端口一致。
# 明确填写 Console 地址时无需发现，可保持关闭；不需要公网映射。
discovery_enabled = false
discovery_port = 4604

[applications]
# TCP + UDP，应用 Render 自动分配范围，起止均包含；耗尽会明确拒绝启动。
# 按本机实际可用端口递增分配，自动跳过已占用端口；不同机器可以复用相同范围。
# Console 节点端口填 0 使用此范围；指定端口也必须属于本机范围。
# 同机不得与管理、桌面或 RTC 范围重叠；90 机器的 4608–4612 为预留端口。
port_start = 4613
port_end = 4998

[rtc]
# UDP，浏览器实时会话的本机媒体端口池，起止均包含。
# 需要从浏览器访问本机时放行此范围；不使用浏览器会话时无需公网映射。
# 每台渲染机器独立设置；与 Console 的 RTC 服务范围不是同一个配置项。
port_start = 5000
port_end = 5031
# 仅独立启动 Render 时填写；由 Service 管理时，Panel UI 中的 IPv4 节点公网地址会自动注入。
# 填写 Render 自己可被浏览器访问的 IPv4，不填访问者地址、Console 地址或端口。
advertised_ipv4 = "203.0.113.10"
```

这里调整的是节点监听端口。当前实现按内外相同端口工作，不支持把不同公网映射端口写入这些监听字段来替代映射。高级异号端口映射仍在实施计划中。

## 自动上报与生效范围

Service 在授权连接建立后立即发心跳，并每 3 秒报告节点访问地址、桌面端口、应用端口范围和 RTC 端口范围。应用实例的实际分配端口沿用实例启动回执及心跳报告。管理端口不会下发给客户端。

Console 的桌面 / 应用连接票据及桌面墙从当前 Service 会话读取节点端点，不再以 Panel 旧桌面链接作为路由来源。主机名、IPv4 和带方括号的 IPv6 可填写到 UI 中；这不等于所有网络拓扑都已验收。

报告是当前连接的运行状态，不写回设备数据库。重连后等待新报告；旧连接不能覆盖新连接，断开或超过 30 秒未更新时不再用于新连接。访问地址为空、报告缺失或非法时明确不可连接，不猜地址，也不退回旧链接。

部署时需同步升级 Panel、Service 和 Console，并在每台节点的 UI 中填写节点公网地址。已有节点授权信息仍可使用，但本功能不会绕过授权。Console 服务连接查询接口可查看 `node_endpoints`，旧设备列表的地址展示及按 IP 搜索尚未迁移，不应当作新端点的权威来源。

Service 在 Console 首次成功认证后，将节点授权使用 Windows DPAPI 保存到本机 Service 数据目录；文件不是 TOML 配置，也不含明文 appkey。重启时先恢复这份已验证授权。Panel 后续传入的空授权会被忽略；新的 appkey 可尝试认证，若 Console 明确返回未授权，Service 会恢复上一份已验证授权并忽略同一失效 appkey 的重复推送。若 Console 拒绝已验证授权，Service 删除本地加密副本并等待新的有效节点授权。

Native 使用控制与文件通道加 UDP 音视频；Web 使用 RTC。配置文件不提供强制选择通道的选项。
