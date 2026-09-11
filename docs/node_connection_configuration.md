# 节点连接配置

普通部署只修改 `px_service.exe` 同目录的 `px_service.toml`：

```toml
console_url = "https://console.example.com:4600"
access_host = "render.example.com"
```

Console 可以在独立机器上；`access_host` 必须指向当前 Render 节点，不能填 Console 地址代替。每台节点独立配置并完成授权，Console 不重复维护同一份地址。

当 `access_host` 是 IPv4 时，Service 会把它作为该节点 Web RTC 直连的受控通告地址传给 Render。域名仍可用于 Native 和 Console 路由；Web RTC 的域名解析 / IPv6 通告尚未实施，当前应在独立 Render 的 `[rtc].advertised_ipv4` 明确填写可达 IPv4，或使用 IPv4 `access_host`。

修改后重启 Service、Panel 和受影响的 Render。地址非空时优先于旧界面缓存。空值仅保留未配置/现有注册入口，不表示已经具备公网访问能力。配置不是自动授权，不保存管理员密码。

## 高级监听配置

只有默认端口冲突或部署约束时，才将以下相应配置段追加到两项地址之后。省略整个配置段使用默认值；填写端口范围时起止两项都必须提供。

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
port_end = 4999

[rtc]
# UDP，浏览器实时会话的本机媒体端口池，起止均包含。
# 需要从浏览器访问本机时放行此范围；不使用浏览器会话时无需公网映射。
# 每台渲染机器独立设置；与 Console 的 RTC 服务范围不是同一个配置项。
port_start = 5000
port_end = 5299
# 仅独立启动 Render 时填写；由 Service 管理时，IPv4 access_host 会自动注入。
# 填写 Render 自己可被浏览器访问的 IPv4，不填访问者地址、Console 地址或端口。
advertised_ipv4 = "203.0.113.10"
```

这里调整的是节点监听端口。当前实现按内外相同端口工作，不支持把不同公网映射端口写入这些监听字段来替代映射。高级异号端口映射仍在实施计划中。

## 自动上报与生效范围

Service 在授权连接建立后立即发心跳，并每 3 秒报告节点访问地址、桌面端口、应用端口范围和 RTC 端口范围。应用实例的实际分配端口沿用实例启动回执及心跳报告。管理端口不会下发给客户端。

Console 的桌面 / 应用连接票据及桌面墙从当前 Service 会话读取节点端点，不再以 Panel 旧桌面链接作为路由来源。主机名、IPv4 和带方括号的 IPv6 可填写到配置中；这不等于所有网络拓扑都已验收。

报告是当前连接的运行状态，不写回设备数据库。重连后等待新报告；旧连接不能覆盖新连接，断开或超过 30 秒未更新时不再用于新连接。访问地址为空、报告缺失或非法时明确不可连接，不猜地址，也不退回旧链接。

部署时需同步升级 Console 和 Service，并填写每台节点的 `access_host`。已有节点授权信息仍可使用，但本功能不会自动创建节点身份或绕过授权。Console 服务连接查询接口可查看 `node_endpoints`，旧设备列表的地址展示及按 IP 搜索尚未迁移，不应当作新端点的权威来源。

Service 在 Console 首次成功认证后，将节点授权使用 Windows DPAPI 保存到本机 Service 数据目录；文件不是 TOML 配置，也不含明文 appkey。重启时先恢复这份已验证授权。Panel 后续传入的空授权会被忽略；新的 appkey 可尝试认证，若 Console 明确返回未授权，Service 会恢复上一份已验证授权并忽略同一失效 appkey 的重复推送。若 Console 拒绝已验证授权，Service 删除本地加密副本并等待新的有效节点授权。

Native 使用控制与文件通道加 UDP 音视频；Web 使用 RTC。配置文件不提供强制选择通道的选项。
