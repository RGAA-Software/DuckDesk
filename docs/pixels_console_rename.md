# Pixels Console 全面改名与升级兼容

## 目标

原产品名 **Pixels CMS** 统一改为 **Pixels Console**。新代码、新部署和用户界面只使用 Console 命名；旧名称仅保留在明确标注的升级兼容入口中，不再作为新接口使用。

## 标准命名

| 范围 | 标准值 |
| --- | --- |
| 产品显示名 | `Pixels Console` |
| Rust 包 | `px_console_server` |
| 服务端程序 | `px_console.exe` |
| 配置文件 | `px_console.toml` |
| 服务端源码 | `rust_server/px_console_server` |
| Web 工程 | `web/px_console` |
| C++ SDK | `src/px_deps/px_console_client` |
| 部署目录 | `output/px_console` |
| 本地 KV 目录 | `storage` |
| WebSocket 前缀 | `/console` |
| Console REST 根路由 | `/api/v1/console/control` |
| 授权产品值 | `console`、`pixels_console` |

Rust 类型采用 `Console*`，C++ 命名空间采用 `px_console`，环境变量采用 `CONSOLE_*` 或 `PX_CONSOLE_*`。

## 兼容策略

兼容项只用于已有部署升级，所有新调用应使用标准命名。

| 旧入口 | 兼容行为 |
| --- | --- |
| `px_cms.toml`、`cms_port` | 新配置不存在时读取旧文件；旧端口键通过 serde alias 解析 |
| `output/px_cms` | 构建和打包脚本把配置、证书、上传文件及授权缓存复制到 `output/px_console` |
| `cms_storage` | 打包时迁移为 `storage`；手工原地升级时仍可直接读取旧目录 |
| `/cms/client`、`/cms/panel`、`/cms/service`、`/cms/website` | 服务端继续接受；新客户端先连接 `/console/*`，旧服务不支持时自动回退 |
| `/api/v1/cms/control` | 与新 Console REST 路由使用同一处理器 |
| `cms://access##`、`cms_srv_config`、`srv_cms_port` | UDP 发现和 Panel 解析继续兼容；同时广播新格式 |
| `cms_host`、`cms_port`、`cms_ssl` | `px_client` 命令行继续接受；标准参数为 `console_*` |
| Panel 的 `cms_*` 设置键 | 首次读取后写入新的 `console_*` 键，清理操作同时覆盖新旧键 |
| `Pixels_cms`、`cms` 授权产品值 | 查询时归一化到新产品值；新授权只写入标准值 |
| `CMS_PROXY_TARGET` | Vite 开发代理临时接受；优先使用 `CONSOLE_PROXY_TARGET` |

Protobuf 消息的字段编号和枚举数值保持不变，因此现有二进制消息的 wire format 不变。源码包名、类型名和生成文件名使用 Console 命名。

## 构建

仅构建正式客户端及文件传输插件：

```bat
scripts\build_px_client.bat build_official 8
```

构建并部署 Console Web、Rust 服务端、媒体服务与 TURN：

```bat
build_px_console_server.bat
```

首次完整打包可运行：

```bat
scripts\package_px_console_server.bat
```

仅更新 Web：

```bat
build_console_web.bat
```

上述服务端脚本会检查并部署 `px_media.exe`、`px_turn.exe`、`config.ini`、`turnserver.conf` 和 `COTURN_LICENSE`。

## 启动

无界面服务模式：

```bat
cd output\px_console
px_console.exe --running-mode=server
```

Panel 模式直接运行：

```bat
output\px_console\px_console.exe
```

默认管理页面为 `https://127.0.0.1:30500`，健康检查为 `https://127.0.0.1:30500/ping`。自签名测试证书需要客户端忽略或手工信任。

## 验收清单

1. `cargo test -p px_console_server` 全部通过。
2. `cargo test -p px_auth_server` 全部通过。
3. 在 `rust_client` 运行 `cargo test --workspace` 全部通过（允许仓库中明确标记的 ignored 测试）。
4. 在 `web/px_console` 运行 `npm run test:unit -- --run` 和 `npm run build`。
5. 构建 `px_client`、`px_console_client` 和 `px_panel`。
6. 检查部署目录中的 Console 程序、配置、Web、媒体与 TURN 产物。
7. 启动服务，确认 `/ping` 返回 200，日志显示 `px_media` 与 `px_turn` ready。
8. 用新 `/console/*` 路由连接；再用旧 `/cms/*` 客户端验证升级兼容。
9. 确认已有 `cms_storage` 授权仍可加载，迁移部署使用新的 `storage` 目录。
10. 执行残留扫描，旧名称只允许出现在本节列出的兼容代码、历史文档及第三方组件固有名称中。

## 边界

`third_party`、`3rdparty`、`src/px_deps/px_3rdparty` 和外部工具二进制不属于产品改名范围，不修改其源码、文件名或许可证内容。
