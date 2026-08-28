# Parsec VDD 迁移验收报告（2026-08-27）

## 1. 结论

本轮迁移验收通过：USBMMIDD 运行方案已删除，虚拟显示后端切换为 Microsoft WHCP 签名的 Parsec VDD，产品控制程序为 `px_display.exe`。虚拟屏按需创建，产品上限统一为 8，不预创建虚拟屏或客户端窗口。

最终安装包已在 `10.0.0.90` 静默覆盖安装并完成驱动、服务、WebClient、原生客户端和容量测试。最终状态为 0 块 Pixels 自有虚拟屏，无测试屏残留。

## 2. 交付物

- 版本：`3.3.59`
- 安装包：`output/build_official/3.3.59/Pixels_3.3.59_Setup.exe`
- 安装包大小：`326199270` bytes
- 安装包 SHA-256：`6930578A64B661D21D6B9FF8E8ED7690D1137BFCE8D5ECEC707B81149E548990`
- 压缩载荷 SHA-256：`82DA0771D6199A039521E81704006395C4FF05A185E48D7334C6B573D505F0E5`
- 上游源码目录：`D:/source/GoCloud/parsec-vdd`
- 固定上游 commit：`a827c7137659b618d0a65f261ad8b2da1c74f772`
- 实施设计：`docs/parsec_vdd_migration_implementation_and_acceptance_plan.md`

安装包内已直接检查存在：

- `px_display.exe`
- `px_service.exe`
- `px_render.exe`
- `px_client.exe`
- `web_client/index.html`
- `parsec_vdd/nefconw.exe`
- `parsec_vdd/driver/mm.inf`
- `parsec_vdd/driver/mm.cat`
- `parsec_vdd/driver/mm.dll`

## 3. 容量统一

以下位置已统一到公共容量 8：

- Parsec 控制器 `MAX_DISPLAYS`
- Rust 服务所有权和槽位上限
- Render 下发的 `virtual_display_max_count`
- 原生客户端状态、命令行默认值和动态 `PxRenderView` 容量
- Direct RTC 预协商轨道槽位
- WebClient 状态兜底、数量显示和按钮门控

该数值表示容量上限，不表示预分配数量。连接初始仍只创建主 `PxRenderView`，收到更多显示器配置后才按需扩展。

## 4. 本地构建和自动测试

| 项目 | 结果 |
|---|---|
| `px_render` Release 增量编译 | 通过 |
| `px_client` Release 增量编译 | 通过 |
| `test_client_virtual_display` | 27/27 通过 |
| `cargo test -p px_service` | 62/62 通过 |
| WebClient voice state | 19 项断言通过 |
| WebClient Vitest | 11/11 通过 |
| WebClient production build | 通过 |
| Parsec 驱动包哈希校验 | 通过 |
| NSIS 安装包制作 | 通过 |
| `git diff --check` | 通过 |

## 5. `build_official/dist` 发布门禁

最终运行产物均已同步至 `build_official/dist`，并与构建树逐项比对：

| 产物 | SHA-256 | 构建树与 dist |
|---|---|---|
| `px_client.exe` | `30837747C61E548CB207CD6DF23B40B4356408C29904B8D6149B8D79AA6A346A` | 一致 |
| `px_render.exe` | `362DB0E849D8366C0CAD249B0234A0920D67CEAE1A3BA9AD1D623963693880BF` | 一致 |
| `px_service.exe` | `A363EBD53E74BA28B749B0CD4BDF5B2CFB8ACC0861F03A34CE5C0D9F54637552` | 一致 |
| `px_display.exe` | `82B92E1E3D737D123CC306EFC1FB637B28C6612FF1191D1F746FF4B7A07579B4` | 一致 |

WebClient 发布目录共 5 个文件，源码 production `dist` 与 `build_official/dist/web_client` 的逐文件 SHA-256 不一致数为 0。

## 6. 90 机器安装验收

安装方式：最终包 `/S` 静默覆盖安装。

结果：

- 安装器退出码 0。
- `validate_voice_install.ps1` 最终 `passed=true`。
- `px_service` 正常运行。
- `px_panel` 位于交互 Session 1 且响应正常。
- `px_render` 位于交互 Session 1 且响应正常。
- `px_display` 位于交互 Session 1 且响应正常。
- 端口 `20375`、`20371`、`20369` 正常监听。
- 安装目录核心文件 SHA-256 与本地 `build_official/dist` 一致。

远程静默安装最初从 Session 0 拉起了一个 Panel；结束该临时 Panel 后，现有 `px_function` 在交互 Session 1 正常重建 Panel。复验全部通过。这是远程静默安装上下文差异，正常交互安装不会使用该临时 Session 0 UI。

## 7. 驱动和旧方案验收

90 机器最终驱动状态：

- 设备：`Parsec Virtual Display Adapter`
- 状态：`OK`
- 版本：`0.45.0.0`
- Provider：`Parsec Cloud, Inc.`
- PnP 签名状态：`IsSigned=true`
- `mm.cat` / `mm.dll` Authenticode：`Valid`
- 签名者：`Microsoft Windows Hardware Compatibility Publisher`
- 不要求 Windows Test Mode

90 原先已有外部 Parsec VDD。安装前后 `HKLM/Software/Pixels/VirtualDisplay/ParsecVddOwned` 均不存在，安装器正确复用外部驱动且没有接管其所有权；产品卸载不会删除该外部驱动。

USBMMIDD 最终状态：

- 安装目录无 `usbmmidd_v2` 载荷。
- USB/Amyuni 遗留显示设备数为 0。
- 运行代码、安装代码和收集脚本不再使用 USBMMIDD。

## 8. 10 轮和容量实机测试

远程证据：`C:/Windows/Temp/PixelsAcceptance/parsec_vdd_final_installer_10_rounds.json`

- 10 轮 create/remove 全部通过。
- 每次创建确认 `1920x1080@60Hz`。
- 每轮 generation 单调递增，从 57 到 76。
- 随后逐级创建 1 至 8 块，generation 77 至 84。
- 第 9 块明确拒绝，错误码为 `VIRTUAL_DISPLAY_LIMIT_REACHED`。
- 8 块按 LIFO 全部删除。
- 最终 `owned=0`、`actual=0`。
- 测试结束时 Service、Panel、Render、Display Controller 均响应正常。

## 9. 已成功增屏却显示超时的修复验收

问题不是 Parsec VDD 创建慢。90 的实测创建通常约 1 秒，其中一条完整 Service 记录为 634 ms；原故障发生时，Windows 显示拓扑重建清除了 Render 状态队列中尚未执行的 Service 成功回调。驱动和画面已经就绪，但客户端一直等不到完成通知，最终错误显示 `SERVICE_TIMEOUT`。

修复后，Service 回调直接在受互斥保护的请求协调器中落地结果，不再依赖会在拓扑重建时被替换的状态队列；同时增加操作分级的后备超时、权威状态对账和带耗时的诊断日志。较长超时只是异常兜底，不会让正常增屏等待到该时限。

真实 WebClient 最终 10 轮证据：`tests/artifacts/virtual_display_timeout_fix_final_20260827/round_01` 至 `round_10`。

- 10/10 轮增屏、拓扑更新、切换虚拟屏、画面采集、切回物理屏和删屏全部通过。
- 每轮操作结束时 `pending=false`，没有 `SERVICE_TIMEOUT` 或 `CAPTURE_REBUILD_TIMEOUT`。
- 失败复现曾稳定证明第 4 轮丢失回调；应用直接协调器修复后，同一路径连续 10 轮通过。
- 最终安装包覆盖安装后的独立冒烟证据为 `tests/artifacts/virtual_display_timeout_fix_final_20260827/post_installer_smoke_final/result.json`：虚拟屏切换期间同一 RTC 视频轨新增解码 8 帧，切回和删除均通过，最终 `owned=0`。
- WebClient 验收脚本记录相邻阶段的逐视频轨计数，避免静态虚拟桌面已在切屏期间交付关键帧、随后按需停止重复编码时被误判为无画面。

## 10. WebClient 真实采集验收

最终安装包单屏热插拔证据：

`tests/artifacts/parsec_vdd_final_installer_webclient_e2e_20260827/result.json`

通过内容：

1. 物理屏初始采集和解码持续增长。
2. 页面收到 `maximum=8`。
3. 新增虚拟屏后拓扑和 generation 更新。
4. 切换到虚拟屏后 `videoWidth=1920`、`videoHeight=1080`，解码帧增长。
5. 切回物理屏后解码帧增长。
6. 删除虚拟屏后恢复单物理屏且画面继续采集。

四虚拟屏逐屏切换证据：

`tests/artifacts/parsec_vdd_final_webclient_four_virtual_e2e_20260827/result.json`

- 创建 `DISPLAY5`、`DISPLAY6`、`DISPLAY7`、`DISPLAY8`，证明产品不再停在 2 块。
- 四块虚拟屏均逐个切换并验证 `framesDecoded` 增长。
- 切回物理屏后仍有有效画面。
- 四块虚拟屏全部逆序删除。
- 最终 `owned=0`、显示器列表恢复为物理屏、视频继续解码。

## 11. 原生 Windows 客户端验收

原生客户端严格从 `build_official/dist/px_client.exe` 启动。

- Direct RTC：单屏连接后远端动态增屏，客户端按需扩为 2 个 `PxRenderView`；两路首帧、UI 渲染、音频和文件通道通过；删屏后驱动状态恢复为 0。
- 标准 RTC：在物理屏加一块 Parsec 虚拟屏的拓扑下，客户端收到 2 屏配置、动态窗口容量扩为 2；标准 RTC 的当前屏单视频轨首帧、UI 渲染、音频和文件通道通过。
- 标准 RTC 使用单视频轨切换显示器；Direct RTC 使用预协商的稳定多轨槽位。验收脚本分别使用对应协议判据，不再把标准 RTC 误判为必须同时收到多轨首帧。

## 12. 最终远程状态

- Pixels 自有虚拟屏：0
- Parsec VDD 设备：1，状态 OK
- USBMMIDD 设备/载荷：0
- Panel、Render、Service、Display Controller：运行并响应
- 安装目录核心哈希：与最终 `dist` 一致
- C 盘剩余空间：约 3.59 GiB

## 13. 已知非阻断项

- WebClient production bundle 仍有 Vite 大 chunk 警告；不影响本功能运行。
- npm audit 中的既有依赖风险不属于本次 Parsec VDD 迁移，但后续应作为独立供应链治理任务处理。
- Parsec VDD 驱动二进制不是开源驱动源码；本项目编译和维护的是控制器 `px_display.exe`，驱动本体使用固定哈希的 Microsoft WHCP 签名发行件。
