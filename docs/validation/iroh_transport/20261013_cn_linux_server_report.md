# CN Linux Server 完整安装和覆盖升级

用户选择代号CN部署Linux Server，并已放行公网端口。2026-10-13完成真实主机安装、同完整包覆盖升级、公网管理登录及低负载iroh Relay传输。BJ和90现有部署保持，CN原7个业务容器的ID、镜像及启动时间逐项不变；没有借用原Auth或Agent Bridge数据库。

管理地址：[CN Console](https://49.232.190.218:4600)。这是独立部署`897996a2-6f29-43a0-ab60-7f5a507404f7`，保留包内4路入门授权；不是90的80路业务部署，也没有迁移节点或应用。完整身份见[交付JSON](20261013_cn_linux_server_delivery.json)。

## 当前安装

| 项目 | 当前值 |
| --- | --- |
| 主机 | CN，49.232.190.218，Ubuntu24.04.4/x86_64 |
| Server | 1.0.56，完整fast-release候选，编译18jobs |
| Compose项目／目录 | `pixels-server-cn`／`/opt/pixels/server-cn` |
| 持久卷 | `pixels-server-cn_pixels_config`、`pixels-server-cn_pixels_data` |
| 外部PostgreSQL依赖 | 独立`pixels-server-cn-postgres`项目，18.6，`/opt/pixels/server-cn-postgres` |
| PostgreSQL数据 | 新卷`pixels-server-cn-postgres_postgres_data`，仅绑定`127.0.0.1:5432` |
| 公网端口 | Console TCP4600、iroh Relay TCP/UDP4605 |
| 数据库／Backup目标schema | 42／42 |
| 原CN业务容器 | 7个，均无重启或替换 |

Docker29.1.3及Compose2.40.3已存在。本机将完整Linux包传到CN并核对SHA后，使用包内`deploy.sh`加载镜像和运行Compose；覆盖也走完整包入口。PostgreSQL镜像复用已存在的准确18.6镜像内容，但启动了独立容器和数据卷；原Auth数据库完全不修改。主机UFW原来拦截新增端口，新增4600/TCP和4605/TCP/UDP规则后，外部HTTPS登录及网页200通过，5432仍仅回环监听。

最终完整包：[PixelsServer_1.0.56_Linux.tar.gz](D:/GoCloud/GammaRayPremium/.cache/cn-server-20261013/PixelsServer_1.0.56_Linux.tar.gz)，55,744,363字节，SHA-256`ec3ee1fb17513425f90c0bdb20bff65a55112fca7b5c348b6030faa038d4eced`。镜像ID`sha256:8b88bc9889d79bdb1b75733145cc5bc1c7426eebf2d45f82e4e355fd0f301177`，41项候选及5项Compose包内容验证，运行中的5个程序SHA与构建包相同。没有升版或发布更新源，不称正式优化发行。

## 修复的实际安装缺陷

首次初始化已经创建配置和数据库，但Console以10001用户启动时无法创建`/var/lib/pixels/logs`，报Permission denied，setup等不到Console就绪。原初始化只给录像缓存和Backup分配权限，遗漏持久日志目录。现在初始化创建日志目录并分配10001:10001、700；升级入口也补齐该目录。数据根仍由root持有，没有放宽为全局可写。实际服务日志写入和进程启动已通过。

完整修复包接续安装时，还发现部署脚本对尚未完成的初始化调用了Backup配置升级，因没有`setup.complete`而拒绝。现在只有完成初始化的部署才走该升级；失败初始化继续由setup-server读取原配置身份并完成。保留初始失败包和第一次修复包，没有删除配置、重建数据库或修改升级程序的完整性拒绝条件。

Linux真实镜像与独立PostgreSQL回归通过：初始化日志目录10001:10001/700、服务UID真实写入、41→42、重复42、异常Backup计划在数据库修改前拒绝，以及自有测试资源清理。修复代码和Shell语法、Rust格式、Python编译与diff检查通过。没有扩大到无关Windows编译、录制或剪贴板验收。

## iroh 配置和少量传输

按现有配置接口启用Console iroh策略和受Console管理的Relay。Relay继续使用首次初始化产生的登记凭据；复制该部署的现有HTTPS证书/私钥到Relay专用权限目录，保留原Console文件权限，没有新的签发系统或短期传输票据。自签名CA只用于该私有部署，客户端校验CA链、期限与主机名；Python管理验收沿用现有的严格AKI标志处理，没有关闭TLS校验。

`https://49.232.190.218:4605/`实际报告fresh/ready、QAD4605、未排空。两个本机probe端点强制Relay，4次可靠往返、双向各4个数据报成功，原QUIC连接和流保持。可靠负载2MiB分20秒发送，实测19.719秒约0.851Mbps，内容校验成功；不是吞吐上限、视频FPS或丢包率承诺。Relay上报累计上传2,188,081／转发2,188,020字节，测试结束连接数0，owned probe已退出。

本批没有把任何Windows节点迁到CN，也没有在CN运行真实GameHook/WebView视频。以上是公网Relay可靠流和数据报的低负载连通证据，复用既有媒体消费者验收，不以CN的4Mbps链路强求60FPS。

Backup实际控制连接为true、告警空、连续失败0，已有独立恢复集`0bc5f171-df0e-477c-8307-5dade3f54254`状态verified。其数据和恢复集在覆盖后保留，没有新增独立恢复业务验收。

## 覆盖和保留

首次完成后启用iroh，再用相同完整包执行`deploy.sh`覆盖。实际输出`UPGRADE_READY current=42 target=42`及`SCHEMA_READY version=42`，setup exit0；Console、Relay、Backup和独立PG均Running。18个不可变配置文件SHA完全相等，包括环境、证书、许可证、登记凭据、CA及workspace密钥。Backup JSON经jq重写后字节SHA改变，但规范化内容完全相同、目标schema42，未把格式变化说成字节不变。日志目录仍10001:10001/700。

原7容器镜像和启动时间完全相同，BJ只做过之前的只读检查，90未迁移。本机probe0、自有隔离PG回归资源清理；CN新Server及其持久卷/外部PG作为本次交付保留运行。初始失败日志、修复包、配置备份和覆盖快照留在自有目录。完整包三次上传约24–32Mbps，此为上传方向，不能代表CN公网出站容量。

初始缺陷与工具样本均记录：缺日志目录、失败初始化被误当升级、PG旧标签缺失、Cargo缓存挂载遮住工具链、主机UFW未放行。最后两项分别修正构建缓存挂载和追加防火墙规则；没有重放未知安装请求，也没有隐藏初次失败。登录凭据、数据库密码、环境文件和私钥不入Git。

CN安装/覆盖与基础传输交付完成。复杂NAT、规模和长稳继续保留既有证据边界；本次没有恢复短顿、网卡或饱和混传专项，Android/Company排除。Remote独立安装目标仍未提供，不自动替换现有桌面产品。
