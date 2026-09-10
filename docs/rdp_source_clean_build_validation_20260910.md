# RDP 源码依赖干净构建验证（2026-09-10）

## 范围

使用新的 `.cache/rdp_clean_20260910/`，不复用上一轮 SDK、FreeRDP 对象文件或 Client 对象文件。
FreeRDP 源码使用固定子模块及经过摘要/diff 校验的补丁副本；源码本身不做额外修改。
SDK 构建进程设置 `VCPKG_BINARY_SOURCES=clear`，重新编译其依赖，不读取旧 vcpkg 二进制包。
依赖工具链使用仓库本地 `.cache/rdp_vcpkg`；它的源码和 bootstrap 工具可以复用，不代表复用编译产物。
Qt、CEF、Client 的其他既有开发依赖不属于此次 FreeRDP 依赖迁移，仍使用既有安装。

不启动发布版全量打包脚本，不变更版本号，不构建 Rust/npm，不改动 90 部署、账号或会话。
测试运行上限 10 分钟，与首次下载/编译时间分开记录。

## 构建命令

在 PowerShell 中：

```powershell
$env:VCPKG_BINARY_SOURCES = 'clear'
.\scripts_build\build_cpp_rdp_sdk.bat third_party/freerdp/source .cache/rdp_clean_20260910/build .cache/rdp_clean_20260910/sdk .cache/rdp_vcpkg
```

Client 的独立构建目录为 `.cache/rdp_clean_20260910/client`，配置
`GAMMARAY_RDP_SDK_ROOT=<仓库>/.cache/rdp_clean_20260910/sdk`，通过 `scripts_build/build_cpp_tests.bat`
构建 `px_client` 和 RDP 测试目标，不使用旧测试 EXE。

代理策略使用 `scripts_build/build_cpp_rdp_policy.bat <SDK> <SDK> <全新策略构建目录>`。
发布脚本新增可选 `-DistDir build_official/dist`，使隔离构建产物仍交付到用户日常启动目录。

## 结果

1. SDK 全量源码构建退出 0。12 个 vcpkg 包从源码处理，二进制缓存关闭；依赖阶段约 9.9 分钟，
   其中 OpenSSL 约 7.5 分钟。SDK 的头文件、导入库、许可证、版本、补丁和运行文件摘要检查通过。
2. SDK 的 CMakeCache 不包含 `C:/source/vcpkg`、`D:/dolit/rdp` 或上一轮验证目录。
   FreeRDP proxy 的 `--version` 正常退出，报告 3.31.0 / aa8650b。
3. 全新策略构建目录编译 `rdp_proxy_policy` 通过；全新 Client 目录完成 404 个构建步骤，
   `px_client` 及全部所选测试目标成功链接。没有复用旧测试 EXE。
4. CTest **9/9 通过，总耗时 7.46 秒**：

   | 测试组 | 秒 |
   |---|---:|
   | rdp_stream | 4.85 |
   | client_rdp_display_channel | 0.12 |
   | client_rdp_decoder | 0.33 |
   | client_rdp_ui_queue | 0.10 |
   | client_rdp_clipboard_channel | 0.14 |
   | client_rdp_view | 0.63 |
   | client_rdp_clipboard | 0.17 |
   | client_rdp_frame | 0.12 |
   | rdp_route_close | 0.60 |

   覆盖解码、显示协议、输入/Unicode、剪贴板格式和文件数据、排队回调销毁、可靠字节流、单客户端租约及关闭路由。
   这些是本地自动测试，不代表真实远端应用已验收。
5. 将已验证 SDK 提升到默认 `.cache/rdp_sdk`，旧 SDK 保留在本批次 `prior_sdk`。
   新 Client、RDP DLL、语音 DLL、许可证和语言资源已发布到 `build_official/dist`。
   检查发现干净构建还部署了 GPU 编译器/加载器 DLL，因此发布脚本补上了
   `d3dcompiler_47.dll`、`dxcompiler.dll`、`dxil.dll`、`vulkan-1.dll`（源文件存在时发布）。
6. 发布脚本逐文件摘要校验通过；额外扫描 Client 构建目录内的 EXE、DLL、资源和 SDK 清单，
   **127 个文件与 dist SHA-256 一致**；语音 DLL 和许可证另由发布脚本校验。
   在 dist 启动新 `px_client.exe --help`，10 秒内退出，退出码 0，无需点击或远端连接。

主要发布摘要：

- `px_client.exe`：`9EF6B4B1BBCC212A3ED78DD2708AB8D4C8640163F6A3A564223DF4BCDFA5FB19`
- `freerdp3.dll`：`0D88A30670AA57D87E057165B878893A05A0E2A0F1FAAA9886AA8A90E5F87D13`
- `winpr3.dll`：`5666D04370047627B760406658463776D4D5F78579C0D393C8A20EE7FA2DBAAB`

本地日志在本批次目录：`client-build.log`、`rdp-tests.log`、`rdp-tests.xml`、`publish-client.log`、`client-help.log`。
旧 dist 的运行文件备份在 `dist_before`，复制后已与原文件核对摘要；没有删除旧 SDK 或旧运行文件。

未执行：90 部署、真实 RDP 登录/画面/音频端到端验收、长时间稳定性或 60 FPS 性能测试。
整个构建时间不等于测试运行时间。本次未提交或 push。

## 补充：90 实机画面验收（2026-09-10 13:04—13:09）

使用上述 `build_official/dist/px_client.exe`，通过 Console 的公开验收应用
`app-24-8cf70180` 自动签发票据并以 `--rdp-launch-stdin` 启动，连接 10.0.0.90:32014。
已核对进程加载的 FreeRDP、OpenH264 和 OpenSSL legacy 模块均来自 dist。
本批次未部署或修改 90 的服务，也未主动重启机器、注销用户或终止其他应用。

- 第二轮 `inst-138-a1a7d730`：实际屏幕截图可见 Windows Server 桌面及远端窗口，
  不是仅凭首帧回调或窗口标题判定；约 60 秒后客户端正常退出，Console 状态 `stopped`。
- 第三轮 `inst-141-6ae654da`：通过客户端发送 Win+R、`cmd /k whoami`、Enter，
  实际截图可见 `win-rass8rc6v3h\grdp_ed5edf1d99a6468` 和对应用户目录；
  验证了基本键盘输入与专用账号身份。脚本随后发送 `exit` 关闭测试命令窗口。
  约 41 秒后客户端正常退出，Console 状态 `stopped`。
- `PrintWindow` 截图在此 GPU 显示路径下产生黑色内容，但同一窗口的屏幕区域截图正常。
  因此本次采用前台目标窗口限定区域的实际截图，不能把 PrintWindow 黑图当作解码黑屏。
- 第一轮 `inst-135-1e7ccf14`（约 73 秒）发生异常断开，清理 API 失败，最终状态为
  `failed / STOP_TIMEOUT`。90 的系统查询显示 `LastBootUpTime=2026-09-10 13:05:50`，
  位于此轮测试时段，说明该轮遭遇远端重启；未进一步确定重启发起方或停止超时的内部原因。
  此轮包含缩放、全屏、最小化/恢复操作，受断开影响，不计为这些功能验收通过。
- 收尾只读检查：三个本地测试 Client PID 均已退出；90 的 32014 无监听，
  无 `freerdp-proxy.exe` 进程；专用用户 Session 2 处于断开状态并保留，
  Administrator console Session 1 未被本测试操作。

本地证据（未纳入 Git）：

- `.cache/rdp_product_trust/run-20260910050624/visible.png`：真实桌面。
- `.cache/rdp_product_trust/run-20260910050736/whoami-visible.png`：真实键盘输入结果及专用用户。
- 各轮同目录 `client.stdout.log`、`client.stderr.log`，首轮目录为 `run-20260910050449`。

结论：新源码 SDK 的真实连接、显示、基本键盘输入和正常退出已验证；
音频听感、剪贴板、文件传输、中文输入、显示模式切换及 60 FPS 不在本轮通过范围内。
保留第一轮停止超时记录，不以之后成功覆盖异常。

## 补充：第二批短时回归（2026-09-10 13:15—13:24）

仍使用同一 dist 源码构建产物、90 和专用公开验收应用，未修改产品代码或远端服务。
截图使用前台客户端窗口的实际屏幕区域；测试脚本、截图和日志保留在本地 `.cache`。

- `inst-144-ce6bf240`，106 秒：窗口调整为 1040×679、进入 1920×1080 全屏、
  最小化后恢复均实际截图正常，连接未中断。此处验证显示切换和画面恢复，
  不单凭窗口尺寸断言 RDP DISP 分辨率协商过程全部正确。
  双向文本剪贴板使用包含中文、emoji 和随机标记的字符串做精确比较；
  远端截图 `CLIPBOARD_IN=True`，本地 `CLIPBOARD_REMOTE_TO_LOCAL=True`。
  本地剪贴板按探针条件恢复。此结果不等于输入法候选框、组合输入或中文直接键入验收。
- `inst-147-c3eb831e`，103 秒：两张动态截图显示彩条、棋盘、中文标签及移动圆形正常，
  fixture 帧号从 207 变为 350；远端结果 `clicks=1, keys=1, wheels=1`。
  `fixture_frames=1198` 是远端测试窗口的更新计数，不是客户端接收帧率，不能据此声称 60 FPS。
  专用远端会话播放短测试音，本地仅测当前 Client PID 的音频会话，
  基线 0、峰值 0.272474、超过阈值的采样 36 次：证明有客户端音频输出活动，
  未做人工听感或音画同步验收。
- 上述两轮客户端均退出码 0，无强制终止，Console 均为 `stopped`，
  stderr 未匹配探针检查的解码/YUV 失败模式。

证据目录：`.cache/rdp_product_trust/run-20260910051539`（显示、文本剪贴板）和
`run-20260910051739`（动态画面、鼠标、键盘、音频）。

生命周期补测 `inst-153-d7de98dd`：`test_rdp_acceptance.ps1 -Scenario GraceReconnect -ObserveSeconds 10`，
29 秒通过；保持原逻辑所有者续签票据，关闭原 Client 后在宽限期内重连成功，
最终客户端离开后观察到实例自然变为 `stopped`（在清理 stop API 之前），
`GraceReconnected=True, GraceExited=True, CodecErrorCount=0`。
正常主动关闭路径出现 `ERRCONNECT_CONNECT_CANCELLED`，没有据此误判连接失败。

文件探针首次 `inst-150-f13b087f` 在 67 秒内报告 `FILES_REMOTE_TO_LOCAL=False`。
只读检查在探针使用的固定用户 Temp 路径下未找到 fixture，但这不是对远端进程实际
`$env:TEMP` 的确认（复核截图可见会话临时目录层级）；不能仅据此证明文件未创建，
也不能把此次失败直接认定为文件协议损坏。该轮不计作文件验收通过。

文件复核 `inst-155-e39efda3`，92 秒：为探针增加发送命令后的实际窗口截图，
可见 Explorer 选中本轮唯一 fixture 目录。
`FILES_REMOTE_TO_LOCAL=True, FILES_LOCAL_TO_REMOTE=True, CONFLICT_CANCEL_EXISTING_PRESERVED=True`。
包含中文、空格路径、空目录层级中的普通文件以及零字节文件；普通文件 SHA-256 为
`F7F12F097969C5C604D20465E3D63C325711531B618B2A3A2C8336BDFE851225`，
双向摘要一致，取消冲突覆盖后目标哨兵内容保留。证据目录为 `run-20260910052202`。
增加截图也增加了等待和前台激活，因此这次成功不能消除首轮失败或证明唯一根因；
首轮命令执行、焦点及探针时序仍需专项复核。

13:24 收尾检查：本批五个实例均为 `stopped`，六个本地 Client PID 均已退出；
90 的 `freerdp-proxy.exe` 数量为 0、32014 监听数量为 0。
专用 Session 2 保持断开未注销，Administrator console Session 1 保持运行。
本轮只创建唯一名称的验收 fixture，未扫描删除旧文件或清理不明归属窗口；
本地证据和测试临时文件保留供追查。本批总墙钟时间约 9 分钟，没有运行长时测试。
