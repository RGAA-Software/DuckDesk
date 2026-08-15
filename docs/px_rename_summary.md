# GammaRayPremium 重命名与改造工作总结

> 本文档总结本会话内对仓库的全部修改：品牌前缀统一（`gr_`/`tc_`/`spvr` → `px_`/`cms`）、服务端 exe/配置改名、证书与密钥、手柄驱动安装器、构建/测试验证，以及各项注意事项。
>
> 生成时间：2026-08。范围：`src/`、`rust_base/`、`rust_client/`、`rust_server/`、`web/`、`setup/`、`scripts/`、`docs/`。

---

## 目录

1. [总览](#1-总览)
2. [已提交阶段（4 个提交）](#2-已提交阶段)
3. [未提交阶段 A：C++/proto/Rust/Web 命名空间重命名](#3-未提交阶段-a)
4. [未提交阶段 B：spvr → cms 全面重命名](#4-未提交阶段-b)
5. [测试](#5-测试)
6. [编译验证](#6-编译验证)
7. [需要你处理的事项](#7-需要你处理的事项)
8. [保留未动的项](#8-保留未动的项)

---

## 1. 总览

| 阶段 | 内容 | 状态 |
|---|---|---|
| 品牌前缀统一 | `gr_*`/`tc_*` 目录、文件、内容 → `px_*`；18 个 `tc_*` 子库去子模块化 | ✅ 已提交（`3217dac5` 等 + `68ddbe57`） |
| 服务端改名 | exe：`px_cms_server.exe`→`px_cms.exe` 等；配置：`px_cms_server_settings.toml`→`px_cms.toml` 等 | ✅ 已提交（`68ddbe57`） |
| 共享证书 | cms TLS 证书改用仓库根共享 `certs/` | ✅ 已提交（`68ddbe57`） |
| 手柄驱动 | `joystick.exe`→`px_joystick.exe`，NSIS 静默安装 | ✅ 已提交（`d3e50931`/`b1509d23`） |
| Qt 翻译 | `windeployqt --no-translations` | ✅ 已提交（`97f5efc9`） |
| C++ 命名空间 | `tc`→`px`、`tccp`→`pxcp`、`tcrp`→`pxrp`、`relay`→`px_relay`、`spvr`→`px_spvr`(→`px_cms`)、`gd`→`px_gd` | ⏳ 未提交 |
| SDK 改名 | `px_spvr_client` → `px_cms_client`（`Spvr*`→`Cms*`） | ⏳ 未提交 |
| spvr 全面清理 | 面板/服务器/proto/Web/文档/脚本中全部 `spvr` → `cms` | ⏳ 未提交（本次） |

当前工作区状态：**1378 项未提交**（142 个重命名 + 1236 个修改），即阶段 A + 阶段 B 的全部改动。

---

## 2. 已提交阶段

### 2.1 `gr_/tc_` → `px_` 重命名 + 服务端改名 + 共享证书（`68ddbe57`）

- **目录/文件改名**：`src/`、`rust/`、`web/` 下全部 `gr_*`/`tc_*` 目录与文件 → `px_*`；18 个 `tc_*` 子目录去子模块化（见 `docs/vendored_deps.md`）。
- **Web 后台**：`web/gr_cms` → `px_cms`，迁移到 Ant Design Vue。
- **服务端 exe 改名**（Cargo `[[bin]] name` 覆盖，crate/package 名不变）：

  | 原 exe | 新 exe | 原配置 | 新配置 |
  |---|---|---|---|
  | `px_cms_server.exe` | `px_cms.exe` | `px_cms_server_settings.toml` | `px_cms.toml` |
  | `px_auth_server.exe` | `px_auth.exe` | `px_auth_server_settings.toml` | `px_auth.toml` |
  | `px_desk_server.exe` | `px_desk.exe` | `desk_settings.toml` | `px_desk.toml` |

- **配置模板**：`rust_server/px_cms_server/src/px_cms.toml`（含顶层镜像 `rust_server/px_cms.toml`），由 `build.rs` 拷贝到 `target/release/`，`build_px_cms_server.bat` 首次启动时种子到 `output/`。
- **构建脚本**：`build_px_cms_server.bat` / `build_px_auth_server.bat` / `build_px_desk_server.bat` —— `SERVER_NAME=px_*_server`、`EXE_NAME=px_cms/px_auth/px_desk`、`%EXE_NAME%.toml` 配置种子、`ensure_tls_cert.bat` 共享证书种子。
- **共享证书**：cms TLS 证书统一使用仓库根 `certs/`（gitignored）；删除 `rust_server/px_cms_server/certs/*`；`build.rs` 指向 `../../certs/`。
- **License 密钥**：生成默认 Ed25519 密钥对 `auth_license_private.key` / `auth_license_public.key`。

### 2.2 手柄驱动安装器（`d3e50931` / `b1509d23`）

- `scripts/collect_dist.py` 第 8 节：把 `src/px_deps/px_controller/vigem/driver/px_joystick.exe` 复制到 dist；`SKIP_NAMES` 含 `px_joystick.exe`。
- `src/px_panel/src/render_panel/px_system_monitor.cpp` `InstallViGem(bool silent)`：静默模式执行 `"{}/px_joystick.exe /S"`，交互模式直接运行。
- `setup/make_setup.nsi`：整文件重写为纯英文/ASCII；安装节末尾 `ExecWait '"$INSTDIR\px_joystick.exe" /S'` 静默安装 ViGEm 驱动；卸载节不卸载驱动（按你的要求）。
- `b1509d23`：替换为你的 `px_joystick.exe` 二进制（959003 字节）。

### 2.3 `windeployqt --no-translations`（`97f5efc9`）

- `px_panel`、`gd_gui` 示例的 `windeployqt` 增加 `--no-translations`（`px_client` 原本已有）；项目自带多语言资源，不部署 Qt 翻译文件。

---

## 3. 未提交阶段 A：C++/proto/Rust/Web 命名空间重命名

### 3.1 C++ 一级命名空间

| 原 | 新 | 说明 |
|---|---|---|
| `tc` | `px` | ~1041 处 |
| `tccp` | `pxcp` | |
| `tcrp` | `pxrp` | |
| `relay` | `px_relay` | 仅 C++ 命名空间；局部 `relay::` 引用区分 |
| `spvr` | `px_spvr`（SDK 内为 `px_cms`） | SDK 最终命名空间 `px_cms` |
| `gd` | `px_gd` | 含 `gd_style_sheet.h` 的 `namespace GD` |

替换要点：正则词边界 `namespace\s+tc\b`（避免误伤 `tccp`/`tcrp`）、`(?<!:)relay::`（避免 `crate::relay::`）、最长前缀优先（`px_capture_d3d11on12` 先于 `px_capture_d3d11`）。

### 3.2 `.proto` package 重命名

| 文件位置 | 原 package | 新 package |
|---|---|---|
| `src/px_deps/px_message_new/*` | `tc` / `tccp` / `tcrp` | `px` / `pxcp` / `pxrp` |
| `src/px_web_client/proto/*` | 同上 | 同上 |
| `src/px_deps/px_server_protocol/relay_message.proto` | `relay` | `px_relay` |

### 3.3 Rust 波纹

- `rust_base/protocol/src/relay.rs` → `px_relay.rs`（`git mv`，生成文件入版本库）；`lib.rs`：`pub mod relay;`→`pub mod px_relay;`、`use crate::relay::RelayMessageType;`→`px_relay`。
- `rust_client`：`tc::`→`px::`、`tcrp::`→`pxrp::`；`proto.rs`：`pub mod tc/tcrp`→`px/pxrp`，include `"/tc.rs"`→`"/px.rs"`。
- `rust_server`：`protocol::relay`→`protocol::px_relay`；修正过一次双前缀 bug（`protocol::px_px_relay::`→`protocol::px_relay::`）；局部 `crate::relay` 模块保留。

### 3.4 Web/TS 波纹

- protobufjs 查找串：`"tc.`→`"px.`、`'tc.`→`'px.`（`src/px_web_client/src/messages/px_proto_messages.ts`、`web/px_web_client/src/rtc/proto.ts`）；`root.nested.tc.`→`root.nested.px.`（`video_player_worker.js`）；注释 `tc.Message`→`px.Message`。
- Rust 侧变量成员访问 `tc.xxx` 不动；macOS `com.tc.*` bundle id 不动。

### 3.5 保留的第三方命名空间（未改）

`amf`、`asio2`、`QWK`、`cereal`、`qrcodegen`、`acss`、`tyti`、`SystemTime`、`fs`（= `std::filesystem` 别名）、`Ui`（Qt）、`px_capture_d3d11*`（已含 px）、`.tooling/zig` 头文件。
（`snowflake_detail` 未定名，待你决定：`px_detail` / `px_snowflake_detail`。）

---

## 4. 未提交阶段 B：spvr → cms 全面重命名

### 4.1 改动前使用清单

全部 207 个文本文件含 `spvr`（大小写不敏感），分布：

| 区域 | 文件数 | 典型内容 |
|---|---|---|
| `rust_server` | 109 | `SpvrContext`、`SpvrApiError`、`gSpvrSettings`、`gSpvrDatabase`、`SpvrServiceMessage`、86 个 `spvr_*.rs` 模块 |
| `src` | 54 | 面板自身命名、SDK、proto、URL、设置键、access 信息 |
| `docs` | 14 | 端口/路径/文件引用、配置键说明 |
| `web` | 13 | `SpvrEvent`/`SpvrUser`/`spvr_*.ts`、`/spvr/*` 路径、vite 代理、px_auth npm 名 |
| `rust_base` | 12 | 生成文件 `spvr_client/panel/relay/service.rs`、proto 镜像 |
| `rust_client` | 3 | `px_service` 的 `protocol::spvr_service`、`/spvr/service`、`spvr_host/port` |
| `scripts` | 4 | `--spvr-host/--spvr-port`、`SpvrAdmin` |

### 4.2 SDK 改名（子阶段 B1，本会话前期）

- `src/px_deps/px_spvr_client` → `src/px_deps/px_cms_client`（`git mv`）；22 个 `spvr_*` 文件 → `cms_*`（api/device/device_api/errors/event/event_api/server_info/stream/user/user_api/user_device/user_device_api）。
- 内容替换：`px_spvr_client`→`px_cms_client`、`Spvr`→`Cms`、小写 `spvr_*`→`cms_*`（107 个文件）。
- 面板网络客户端 `network/px_spvr_client.cpp/h` → `px_cms_client.cpp/h`（`git mv`）。
- CMake：SDK `project(px_cms_client)`、父级 `add_subdirectory(px_cms_client)`、根 `CMakeLists.txt` 链接 `px_cms_client`；`docs/vendored_deps.md` 目录列 → `px_cms_client`（URL 保留）。
- 面板自身 `spvr` 命名当时保留（见 4.3，本次已全部改掉）。

### 4.3 全面清理（子阶段 B2，本次执行）

所有保留项全部改名，含 **142 个路径 `git mv`**（rust_server 86、src/px_deps 28、src/px_panel 15、rust_base 8、web 3、src/px_client 2）与 **1236 个文件内容修改**。逐类明细：

#### a) 面板自身命名
| 原 | 新 |
|---|---|
| `render_panel/spvr/`（`px_spvr_manager.*`、`px_event_manager.*`） | `render_panel/cms/`（`px_cms_manager.*`、`px_event_manager.*`） |
| `render_panel/spvr_scanner/`（`spvr_scanner.*`，类 `CmsScanner`） | `render_panel/cms_scanner/`（`cms_scanner.*`） |
| `render_panel/companion/spvr/`（`spvr_setting.*`、`spvr_access_info_parser.*`） | `render_panel/companion/cms/`（`cms_setting.*`、`cms_access_info_parser.*`） |
| `px_client/network/ct_spvr_client.*`（类 `CtCmsClient`） | `px_client/network/ct_cms_client.*` |
| 标识符 `spvr_client_`、`spvr_scanner_`、`spvr_manager_`、`spvr_host_`、`spvr_port_`、`spvr_connected_`、`using_spvr_host_/port_`、`edt_spvr_*`、`spvr_config_`、`spvr_ip_` 等 | 对应 `cms_*` |

#### b) proto 包 + 生成类型
- `spvr_client`/`spvr_panel`/`spvr_relay`/`spvr_service` → `cms_client`/`cms_panel`/`cms_relay`/`cms_service`（`src/px_deps/px_server_protocol/*` 与 `rust_base/protocol/px_protocol/*` 镜像同步）。
- 消息/枚举类型 `Spvr*`→`Cms*`、常量 `kSpvr*`→`kCms*`（wire 字段编号不变，二进制兼容）。
- C++ 生成文件 `spvr_client.pb.cc/h` → `cms_client.pb.cc/h`（命名空间 `cms_client`）；CMake `protobuf_generate_cpp` 列表同步。
- Rust 生成文件 `spvr_*.rs` → `cms_*.rs`（`git mv`），`lib.rs` 模块声明同步；prost 重新生成后与改名版逐字节核对一致。

#### c) 服务器（rust_server / px_cms_server）
- 86 个 `spvr_*.rs` → `cms_*.rs`（auth/filter/event/device/net_client/net_panel/net_relay/net_service/record/stream/system/user/user_device/config/interact/net_cm 等）。
- 标识符：`SpvrContext`→`CmsContext`、`SpvrApiError`→`CmsApiError`、`gSpvrSettings`→`gCmsSettings`、`gSpvrDatabase`→`gCmsDatabase`、`gSpvr*ConnMgr`→`gCms*ConnMgr`、`SpvrServer`→`CmsServer`、`SpvrAdmin`→`CmsAdmin`、`web_spvr_dir`→`web_cms_dir` 等。
- 配置：`spvr_port` → `cms_port`（`px_cms.toml` 模板 + `cms_settings.rs` 解析 + `main.rs`）。

#### d) URL 路径（客户端 + 服务器 + Web 全链路同步）
| 原 | 新 |
|---|---|
| `/spvr/client` | `/cms/client` |
| `/spvr/panel` | `/cms/panel` |
| `/spvr/website` | `/cms/website` |
| `/spvr/service` | `/cms/service` |
| `/api/v1/spvr/control/...` | `/api/v1/cms/control/...` |
| vite 代理 `/spvr`（ws:true） | `/cms` |

#### e) 设置键 / CLI / access 信息格式
- QSettings 键：`spvr_server_host`→`cms_server_host`、`spvr_server_port`→`cms_server_port`、`spvr_access_info`→`cms_access_info`。
- CLI 参数：`--spvr_host`/`--spvr_port` → `--cms_host`/`--cms_port`（`ct_main_ws.cpp`、`test_webrtc_local.bat`、`inject_service_auth.mjs`）。
- access 信息：前缀 `spvr://access##` → `cms://access##`（服务器 `cms_context.rs` 生成 + UDP 广播、面板 `cms_scanner.cpp`/`panel_companion_impl.cpp` 解析/转发）。
- JSON 键：`spvr_srv_config`→`cms_srv_config`、`srv_spvr_port`→`srv_cms_port`（服务器 serde 结构体 + 面板解析 + 测试镜像同步）。

#### f) Web
- `web/px_cms/src/entity/spvr_event.ts`→`cms_event.ts`（类 `SpvrEvent`→`CmsEvent`）、`spvr_user.ts`→`cms_user.ts`（`SpvrUser`→`CmsUser`）、`model/spvr_api.ts`→`cms_api.ts`。
- `web/px_auth`：npm 名 `spvr` → `px_auth`（`package.json`/`package-lock.json`/`README.md`）。

#### g) 文档与脚本
- `docs/`：端口表（`spvr_port`→`cms_port`）、路径引用、文件引用、proto 名、部署文档配置键等全部同步。
- `scripts/`：`inject_service_auth.mjs`、`service_test_ctl.bat`、`test_webrtc_local.bat`、`cdp_video_wall_test.mjs`（`SpvrAdmin`→`CmsAdmin`）。

---

## 5. 测试

| 测试 | 结果 |
|---|---|
| **新增** `config::cms_access_info::tests::access_info_serializes_with_cms_keys`（锁定 `cms_srv_config`/`srv_cms_port` 线上格式，断言无 spvr 泄漏） | ✅ |
| rust_server `cargo test --workspace` | ✅ 63/63（含改名后的 `cms_ws_token_filter`、`cms_service_conn_mgr` 等既有测试） |
| rust_client `cargo test --workspace` | ✅ 全部通过（含 px_service 断言的 `/cms/service` 端点测试） |
| C++ `test_access_decrypt`（重新生成含 `cms_srv_config` 新键的加密 fixture，解密/解析/加密往返） | ✅ PASS |
| web/px_cms `vitest run` | ✅ 10/10 |
| web/px_auth `npm run test:unit` | ✅ 47/47 |

---

## 6. 编译验证

| 步骤 | 结果 |
|---|---|
| `build_client.bat full`（增量，改名后首轮） | ✅ exit 0 |
| 删除 `build_official/` → `build_client.bat full`（全新编译，验收流程） | ✅ exit 0 |
| rust_server 全 workspace `cargo check` | ✅（prost 重新生成的 4 个 `cms_*.rs` 与改名版一致，仅 1 处生成格式微差，保留生成版） |
| rust_server / rust_client `cargo test` | ✅ 见上 |
| 版本号还原 | ✅ 7 个版本文件 `git restore`，无残留 diff |

> 说明：`build_client.bat` 带 `GR_SKIP_SERVERS=1`，不编译 rust_server 的 release 二进制；服务器以 `cargo check`/`cargo test` 验证。如需 release 服务器产物，请另行执行 `build_official.bat full`。

---

## 7. 需要你处理的事项

1. **运行配置重新生成**：`spvr_port` 配置键已改 `cms_port`，`cms_settings.rs` 解析不到会 panic。请删除 `output/px_cms_server/px_cms.toml`（以及 `output/` 下其它旧 toml），让 `build_px_cms_server.bat` 等重新种子配置。
2. **旧数据不迁移**：QSettings 旧键（`spvr_server_host` 等）、已保存的 `spvr://access##` 字符串、旧 license 中的 `SpvrAdmin` 用户名不会自动迁移（开发环境无影响）。
3. **构建前停服务**：GammaRayService（Windows 服务）会重启 GammaRayRender/UserProxy/SysInfo 导致 DLL 锁定，构建前 `Stop-Service GammaRayService` 并结束相关进程。
4. **发布版服务器**：建议跑一次 `build_official.bat full` 验证三个服务器 release 产物。
5. **版本号**：`set_app_version.py --bump` 每次构建自增（3.3.x），按约定一律还原、不提交。

---

## 8. 保留未动的项

- `docs/vendored_deps.md` 中远端仓库 URL `git@github.com:RGAA-Software/px_spvr_client.git`（真实远程地址，本地目录名为 `px_cms_client`）。
- 第三方命名空间（见 3.5）与 `.tooling/`、`px_3rdparty/` 预编译库。
- macOS `com.tc.*` bundle id。
- `snowflake_detail` 命名空间（待定，建议 `px_detail`）。
- 构建产物目录 `build_official/`、运行目录 `output/`、`certs/`（均 gitignored）。
