# Pixels Official Single Server · Linux

Official 是镜像/归档发行身份，不是固定服务器地址。官方自用与私有部署使用同一包；首次启动自动选取本机网络地址并生成证书，访问地址可为局域网 IP。

安装 Docker Engine/Compose，另行提供 PostgreSQL 18。Single Server 优先使用 TLS，数据库未开启 TLS 时也能连接；不校验数据库 CA/主机名。PostgreSQL 不在 Pixels 包内。

解压版本化 Linux 包后，在解压目录执行一次 `./deploy.sh`。脚本验证包文件、加载镜像，运行配置补齐检查，再执行 `docker compose up -d` 并等待自动初始化完成；不需要预制 env、JSON、证书或许可证。首次安装若已手动加载镜像，也可以直接执行 `docker compose up -d` 并查看 `setup` 容器日志。

首次安装只需先安装 PostgreSQL 18，创建可登录的超级用户 `Pixels`、密码 `Pixels@123`，监听本机标准端口 `5432`。Compose 使用 host 网络，让容器可通过 `localhost:5432` 访问单独安装的 PostgreSQL。初始化自动创建 Console 数据库/角色、配置 TLS、录像缓存、Backup、初始管理员 `Pixels` / `Pixels@123`，并登记 Relay。包内有 Auth 签名的离线 PXLIC2 入门许可证：远程桌面、游戏、WebView、RDP 各同时最多 1 路，总共最多 4 路。随后在 `https://<服务器内网 IP>:4600/` 登录；管理员可在网页导入新 PXLIC2 替换入门授权，无需公网 IP。若数据库或对外地址不是本机默认值，可在执行命令前设置 `PIXELS_SETUP_POSTGRESQL_HOST` 或 `PIXELS_SERVER_ACCESS_HOST` 环境变量。Console 使用部署时生成的自签名 HTTPS CA；私有部署运维可以替换/信任自己的 CA。

版本升级按 Server 先行：保留同一目录内的 Compose 项目名和 Docker 命名卷，执行新包的 `./deploy.sh`。若同一部署尚无 Console↔Backup 控制配置，脚本生成一次专用令牌并补入两个私有配置；以后升级保留该令牌，配置冲突则拒绝升级。不要在需要补齐配置的旧部署上跳过脚本直接运行 `docker compose up -d`。不需要卸载。`./deploy.sh uninstall` 只停止并删除 Pixels 容器；配置、数据、备份与外部 PostgreSQL 保留。不要执行 `docker compose down -v`，除非明确要销毁此部署的数据。

初始化是一次性容器任务，不提供网页或监听端口。Console 和 Relay 分别监听 `0.0.0.0:4600`、`0.0.0.0:4605`，开放范围由运维防火墙决定。Backup 主动连接 Console，不提供入站监听端口。

## Console 备份恢复到隔离新库

Console 网页只显示已验证恢复集及只读预检，不执行数据库恢复。复制页面上的恢复集 UUID，在本部署目录运行：

```bash
./restore_console.sh <recovery-set-uuid>
```

脚本先校验 Console 归档、包内 PostgreSQL 工具和部署身份，再提示输入 `RESTORE`、PostgreSQL 管理员角色与密码。它暂停 Backup，创建固定命名的 `pixels_console_restore_<无横线恢复集UUID>` 新库，恢复并核对 Console 部署身份，最后恢复 Backup。原 `pixels_console`、Console 配置和运行服务均不修改；中途失败保留新库供人工检查，不自动清理。不要把“已恢复到新库”当作“已切换业务”：日常独立恢复集没有自动恢复准入，正式切换须另行维护和权限对账。
