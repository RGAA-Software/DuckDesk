# Pixels Customer Single Server · Linux

安装 Docker Engine/Compose，另行提供可通过 TLS `verify-full` 访问的 PostgreSQL 18。PostgreSQL 不在 Pixels 包内。

解压版本化 Linux 包后，在解压目录执行一次 `./deploy.sh`。脚本验证包文件、加载镜像并执行 `docker compose up -d`，不需要预制 env、JSON、证书或许可证。也可以先 `docker load --input pixels-server-<version>.tar`，再执行 `docker compose up -d`。

首次安装在服务器本机浏览器打开 `http://127.0.0.1:4700/`，填写 PostgreSQL 主机、端口、超级用户、密码、CA PEM、Console 公网主机和首个管理员账号。初始化自动创建 Console 数据库/角色、配置 TLS、录像缓存、Backup，并登记 Relay。随后在 `https://<公网主机>:4600/` 以管理员登录并上传签发的 PXLIC2 许可证。Console 使用部署时生成的自签名 HTTPS CA；私有部署运维可以替换/信任自己的 CA。

版本升级按 Server 先行：保留同一目录内的 Compose 项目名和 Docker 命名卷，加载新版本镜像并执行新包的 `docker compose up -d`。不需要卸载。`./deploy.sh uninstall` 只停止并删除 Pixels 容器；配置、数据、备份与外部 PostgreSQL 保留。不要执行 `docker compose down -v`，除非明确要销毁此部署的数据。

首次网页只绑定主机 `127.0.0.1:4700`，公网不暴露初始化页面；远程安装时通过 SSH 端口转发访问。业务端口为 Console 4600 和 Relay 4605，开放范围由运维防火墙决定。
