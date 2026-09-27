# Pixels Customer Single Server · Linux

安装 Docker Engine/Compose，另行提供可通过 TLS `verify-full` 访问的 PostgreSQL 18。PostgreSQL 不在 Pixels 包内。

解压版本化 Linux 包后，在解压目录执行一次 `./deploy.sh`。脚本验证包文件、加载镜像，运行一次配置补齐检查，再执行 `docker compose up -d`；不需要预制 env、JSON、证书或许可证。首次安装若已手动加载镜像，也可以直接执行 `docker compose up -d`。

首次安装在服务器本机浏览器打开 `http://127.0.0.1:4700/`，填写 PostgreSQL 主机、端口、超级用户、密码、CA PEM、Console 公网主机和首个管理员账号。初始化自动创建 Console 数据库/角色、配置 TLS、录像缓存、Backup，并登记 Relay。随后在 `https://<公网主机>:4600/` 以管理员登录并上传签发的 PXLIC2 许可证。Console 使用部署时生成的自签名 HTTPS CA；私有部署运维可以替换/信任自己的 CA。

版本升级按 Server 先行：保留同一目录内的 Compose 项目名和 Docker 命名卷，执行新包的 `./deploy.sh`。若同一部署尚无 Console↔Backup 控制配置，脚本生成一次专用令牌并补入两个私有配置；以后升级保留该令牌，配置冲突则拒绝升级。不要在需要补齐配置的旧部署上跳过脚本直接运行 `docker compose up -d`。不需要卸载。`./deploy.sh uninstall` 只停止并删除 Pixels 容器；配置、数据、备份与外部 PostgreSQL 保留。不要执行 `docker compose down -v`，除非明确要销毁此部署的数据。

首次网页只绑定主机 `127.0.0.1:4700`，公网不暴露初始化页面；远程安装时通过 SSH 端口转发访问。业务端口为 Console 4600 和 Relay 4605，开放范围由运维防火墙决定。

## Console 备份恢复到隔离新库

Console 网页只显示已验证恢复集及只读预检，不执行数据库恢复。复制页面上的恢复集 UUID，在本部署目录运行：

```bash
./restore_console.sh <recovery-set-uuid>
```

脚本先校验 Console 归档、包内 PostgreSQL 工具和部署身份，再提示输入 `RESTORE`、PostgreSQL 管理员角色与密码。它暂停 Backup，创建固定命名的 `pixels_console_restore_<无横线恢复集UUID>` 新库，恢复并核对 Console 部署身份，最后恢复 Backup。原 `pixels_console`、Console 配置和运行服务均不修改；中途失败保留新库供人工检查，不自动清理。不要把“已恢复到新库”当作“已切换业务”：日常独立恢复集没有自动恢复准入，正式切换须另行维护和权限对账。
