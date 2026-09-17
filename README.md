![](docs/pixels/images/px_text_logo.png)

项目编码规范：[C++ Google Style（4 空格） / Rust 官方风格 / TypeScript Microsoft Style](docs/coding_style.md)。
#### 💖 This repository is the open-source edition of [Pixels on Steam (not released yet)](https://store.steampowered.com/app/2947460/Pixels/). See the [official site](https://pixels.yun) for downloads.
#### 💖 这是 Pixels 的开源版；完整版本见 [Steam（暂未开放下载）](https://store.steampowered.com/app/2947460/Pixels/)或[官网](https://pixels.yun)。

### Showcases
[企业功能演示](https://www.bilibili.com/video/BV1ZGDhBGEAA/)  
[B站演示地址](https://www.bilibili.com/video/BV17mvQexELk/)  
[B站演示地址(远程桌面)](https://www.bilibili.com/video/BV1qF5NzfENv/)  

## Usage
#### [当前产品编译、安装与使用说明](docs/product_build_and_usage.md)

服务改造规划（待实施）：[连接与业务架构](docs/server_refactoring_plan.md) · [独立部署、Official/Customer 发行与升级](docs/server_deployment_and_upgrade_plan.md) · [服务管理与运维后台](docs/service_operations_console_plan.md)

优先实施：[PostgreSQL 数据库改造、备份与升级方案](docs/postgresql_database_migration_plan.md)（先 DB0–DB5，再推进服务拆分与 GPU 调度）。

新基线约束：按全新系统开发，不迁移旧开发数据，不做旧协议/配置/接口兼容、双写或回退；未来正式版本的升级能力单独验收。

已开始落地：[本机 PostgreSQL 开发环境与基础验收入口](deploy/development/postgres/README.md)（独立新库，Auth/Desk 已接入，Console 逐领域实现）。

实际进度：[数据库实施与验收状态](docs/server_database_execution_status.md) · [身份、用户组与事务契约](docs/postgresql_identity_contract.md) · [设备授权](docs/postgresql_device_contract.md) · [应用目录](docs/postgresql_application_contract.md)。
节点与运行时：[节点控制](docs/postgresql_node_contract.md) · [部署与预约](docs/postgresql_deployment_contract.md) · [实例/命令/对账](docs/postgresql_instance_contract.md) · [RDP 工作区凭据](docs/postgresql_workspace_contract.md)。
领域边界：[DB0 领域、权限与恢复契约](docs/postgresql_domain_contract.md) · [Desk 新启动与测试方式](docs/px_desk_web_overview.md) · [Auth 新配置与验收](docs/px_auth_server_runtime_config.md)。
产品接入：[Console PG 运行时与统一切换契约](docs/postgresql_console_runtime_contract.md)（实施边界与未接入能力清单，不是完成声明）。

逐步验收：[开发步骤、测试环境与阶段门禁](docs/server_incremental_validation_plan.md)（先建立基线和隔离 PG 测试，再逐步验证权限、事务、故障恢复与真实客户端）。

交互图解：[打开服务拓扑网页](docs/server_topology.html)（下载后可直接用浏览器离线打开）。

云应用业务规划：[应用管理、机器选择与多 GPU 调度](docs/cloud_application_scheduling_plan.md)；网页第五页可交互查看候选和预约变化。

## More
#### [Official Site (官网)](https://pixels.yun)
#### [Documentation (文档)](https://docs.pixels.yun)

### Pixels
#### ⚡️Stream your game fame and desktop to other devices, and replay gamepad,keyboard,mouse events on the host PC. In a word, It's a alternative of TeamViewer, ToDesk, RustDesk, etc.
#### ⚡️远程操作电脑，云游戏，模拟手柄等，类似ToDesk, 向日葵, RustDesk, TeamViewer的工具

## Support Platforms
✅  Ready  
⌛  Developing

| Platform | Client | Server  |
|----------|--------|---------|
| Windows  | ✅      | ✅       |
| Android  | ⌛ Pixels client rebuild | ⌛       |

## Work Mode (工作模式)
#### 1. Connect Directly (直连)
![](docs/pixels/images/work_directly.png)

#### 2. Relay (转发)
> You can also connect directly in this mode  
> 此模式下亦可直连  

![](docs/pixels/images/work_relay.png)

## Key Features
### Display multiple screens of remote computers
#### Switch display
![](docs/pixels/images/multi-screen_switch_display.gif)
#### simultaneous display
![](docs/pixels/images/multi-screen_switch_imultaneous_display.gif)
### File transfer
#### The transmission speed is very fast
![](docs/pixels/images/file_trans.gif)
### Transferring files via clipboard
#### The transmission speed is very fast
![](docs/pixels/images/clipboard_file_trans.gif)

### Screen Recording
#### Supports simultaneous recording on multiple screens
![](docs/pixels/images/screen_recording.gif)

### Detailed statistical information
![](docs/pixels/images/statistics.gif)

### Cloud/Remote Game Recordings
#### Wukong/黑神话悟空
![](docs/pixels/images/test3.gif)
#### Ori/奥日
![](docs/pixels/images/test1.gif)
#### Elden Ring/埃尔登法环
![](docs/pixels/images/test2.gif)

### Music Spectrum
![](docs/pixels/images/spectrum_1.gif)
![](docs/pixels/images/spectrum_2.gif)
![](docs/pixels/images/spectrum_3.gif)

### Screenshots
![](docs/pixels/images/main.jpg)
![](docs/pixels/images/status.jpg)
![](docs/pixels/images/game.jpg)
![](docs/pixels/images/security.jpg)
![](docs/pixels/images/client.jpg)
![](docs/pixels/images/file_transfer.jpg)
![](docs/pixels/images/client_status.jpg)

### License
##### This project is licensed under the GNU General Public License v3.0 (GPLv3). You may use, modify and redistribute these codes under the terms of the GPLv3, including for commercial purposes, as long as derivative works are also licensed under the GPLv3.
##### 本项目采用 GNU General Public License v3.0 (GPLv3) 开源协议。你可以在 GPLv3 条款下自由使用、修改和再发布本代码（包括商用），但衍生作品必须同样以 GPLv3 开源。
