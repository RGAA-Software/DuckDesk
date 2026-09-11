# px_auth Linux / CN 部署记录

日期：2026-09-10。

本服务作为整体调试环境的统一授权节点；90 部署 Console / 渲染端、本机作为客户端，见
[整体调试环境](debug_environment.md)。

- 目标：代号 CN，49.232.190.218，Ubuntu 24.04 x86_64。
- 域名 `rgaa.vip` 解析至 CN；用户确认沿用既有授权入口 `https://auth.rgaa.vip`。
- 凭据只存本地 `.env/test_machine.md`，不提交密码、生产配置或私钥。
- 原服务：Supervisor `gr_auth_server`，目录 `/opt/gr_auth_server`，HTTPS 30400；Nginx 对外 443。
- 原数据库：本机 MongoDB 的 `db_gr_auth_server`。升级保留数据库、已有账号、授权签名私钥及对外授权地址。
- 本机 WSL 无法正常启动；旧 musl 脚本存在文本损坏且使用 libudev stub，本次不使用。
- 新构建入口：`scripts_build/build_px_auth_linux.ps1`，Cargo Zigbuild，GNU Linux x64，使用 CN 的真实 libudev 动态库交叉链接。
- 前端从 `web/px_auth` 重新构建，与后端共同发布。先备份旧部署和数据库，再切换；失败恢复原服务。

## 部署结果

已完成编译、迁移和公网验收：

- 当前服务：Supervisor `px_auth_server`，PID 2919308（验收时），状态 RUNNING。
- 当前目录：`/opt/px_auth_server`；程序 `px_auth`，配置 `px_auth.toml`，前端 `web_auth/`。
- 正式入口：`https://auth.rgaa.vip`；公网 HTTPS 证书校验通过，首页和 `/api/v1/ping` 返回 200。
- 旧服务 `gr_auth_server` 已停止，其 Supervisor 配置改为 `.conf.disabled-20260910-115548`，不会自动拉起抢占端口。
- Nginx、MongoDB 和域名解析未修改；后端仍监听 HTTPS 30400。
- 旧部署保留在 `/opt/gr_auth_server`，没有覆盖旧程序、配置和证书。
- 备份：`/opt/px_auth_backups/20260910-115548/`，root-only，包含旧部署归档、Supervisor / Nginx 配置和 MongoDB gzip archive。
- 签名私钥、公钥与旧部署 SHA-256 一致；不生成新签名密钥，不重新签发已有授权。
- 前后均为 2 个账号、49 条授权、0 个客户；管理员配置凭据登录、查询 49 条授权、退出登录、匿名拒绝访问全部通过。
- 当前实现启动时按 bootstrap 配置同步账号密码，并重新生成登录 JWT secret；升级后需重新登录。

本地产物与远端安装的 12 个运行文件逐一核对 SHA-256 一致。

`px_auth` SHA-256：`57f836318d55b310233150f17738e892d796c1753b4f70328535f3b17d657758`。

## 构建与检查

- 前端：`npm run build`，成功；`npm run test:unit`，7 个文件 / 47 项测试通过。
- 后端：`cargo test --locked -p px_auth_server --bin px_auth`，Windows 上 89 项测试通过，执行耗时 1.62 秒。
- Linux：`scripts_build/build_px_auth_linux.ps1`，Release GNU x64 首次构建约 4 分钟；CN 实机运行验收通过。
- 不将 Windows 单元测试描述为 Linux 单元测试；Linux 本轮执行的是部署后真实服务验收。
- 本地产物目录：`.cache/px-auth-linux-target/x86_64-unknown-linux-gnu/release/`。
- 交叉工具：Zig 0.14.1、cargo-zigbuild 0.23.0、Rust `x86_64-unknown-linux-gnu` target。
- 首次准备 sysroot：通过已认证 SSH/SFTP 从目标 CN 的 `/usr/lib/x86_64-linux-gnu/libudev.so.1`
  复制到 `.cache/cn-linux-sysroot/lib/libudev.so`；构建脚本提供配套 pkg-config 描述。
  这是链接检查所需的真实库，不打包替换远端系统库；最终程序 `ldd` 无缺失依赖，未保留 libudev 运行依赖。
- 发布使用本轮 `web/px_auth/dist`，不依赖可能陈旧的预编译 assets；前后端一并校验。
- `scripts/deploy/px_auth_cn_remote.py` 是本次 **gr → px 首次迁移** 的远端执行脚本，要求 root、上传目录及 SHA-256 manifest；
  新目录存在时拒绝覆盖，不应作为无条件重复更新脚本运行。
- `.env/` 已纳入 Git 忽略规则；不提交任何服务器凭据、生产配置或私钥。

## 回滚

仅在需要回滚时执行；正常部署完成后不要执行以下操作：

1. `sudo supervisorctl stop px_auth_server`。
2. 将 `/etc/supervisor/conf.d/px_auth_server.conf` 移出 `.conf` 扫描范围；
   将 `gr_auth_server.conf.disabled-20260910-115548` 恢复为 `gr_auth_server.conf`。
3. `sudo supervisorctl reread`，再执行 `sudo supervisorctl update`，检查 `gr_auth_server` RUNNING 和公网健康接口。
4. 数据库未做删除或结构替换。不要自动恢复数据库归档，以免覆盖上线后的新增授权；需要数据恢复时先确认恢复点和期间写入。

脚本在切换后健康检查失败时自动恢复旧 Supervisor 服务，保留失败的新目录和全部备份用于诊断。
