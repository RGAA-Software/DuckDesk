# FreeRDP Proxy 实机验证记录（2026-09-08）

## 范围与结论

链路：本机 Qt demo → `10.0.0.90:13389`（独立 FreeRDP Proxy）→
90 本机 `127.0.0.1:3389`（Windows RDP）。此记录不是 GammaRay 产品集成验收。

**更新至 16:52：代理连接、完整桌面显示及断开重连已通过。**
客户端经 `13389` 收到 Windows 桌面，日志确认 `RDPGFX_CODECID_AVC444v2`；测试客户端退出后，
`usbtest2` 会话 2 保留为断开状态，重连仍回到会话 2，登录时间保持 16:47。
此结果验证了 RDP 图形经 proxy 转发、客户端解码显示的可行性，不代表 GammaRay 集成或 60 FPS 性能验收。

## 最终定位和有效修正

1. **WinPR 内置 SSPI 的 Unicode 包名不匹配。**
   `winpr/include/winpr/sspi.h` 的 `NEGO_SSP_NAME` 使用 `_T("Negotiate")`；Windows 构建定义了 `UNICODE`。
   `Negotiate/negotiate.c` 将该宽字符串写入凭证句柄，而 `sspi_winpr.c` 的上下文初始化/释放路径按 ANSI 包名查找。
   不联网的 `scripts_build/test_rdp_sspi_package.ps1` 实测：获取凭证 `0x00000000`，ANSI 包名为 `N`，
   UTF-16 包名为 `Negotiate`，释放凭证返回 `0x80090305`。这将此前的认证包错误定位到了可复现的实现问题。
2. **本次连接修正采用 Windows 原生 SSPI，不使用临时 SAM 虚拟账号。**
   客户端与 proxy 均设置进程环境 `WINPR_NATIVE_SSPI=1`，代理配置移除 `Server.SamFile`；
   入口使用已有 Windows 测试账号，后端仍独立配置同一测试账号的目标连接凭证。Windows 系统 NLA 保持开启。
   这是有效的认证路径替代，不是对上述 WinPR 缺陷的源码修复；第三方源码保持只读。
   GammaRay 自身身份与 Windows 身份解耦的入口鉴权仍需后续集成，不能把本次相同账号验收当作该能力已完成。
3. **图形通道白名单漏项导致认证通过后黑屏。**
   仅放行 `drdynvc` 不会自动放行全部动态子通道；日志明确显示
   `Microsoft::Windows::RDS::Graphics -> ignored`。补入 Graphics 和 DisplayControl 全名后收到 AVC444v2 图形。
4. **90 的会话初始化曾失败，期间机器发生外部重启。**
   重启前服务器记录 `EvCsrInitialized`、`0x80004005`，画面停在“请等候 本地会话管理器”。
   查询得到机器启动时间 16:46:32；本次脚本未执行重启、注销或系统服务重启。
   重启后同账号直连 3389 和经 proxy 13389 都进入完整桌面。由于环境变化，不能声称已定位或修复此前的系统会话故障根因。

### 自动启动和复现

- 在用户自有 `D:/dolit/rdp/src/MainWindow.cpp` 增加可选环境变量：
  `GAMMARAY_RDP_PROBE_USERNAME`、`GAMMARAY_RDP_PROBE_DOMAIN`、`GAMMARAY_RDP_PROBE_PASSWORD`。
  密码读取后从 demo 进程环境删除，不进入命令行或 QSettings；调用方应在启动后同样清理自己的密码环境变量。
- 启动参数：`--autoconnect --cert-ignore --host 10.0.0.90 --port 13389 --features=clipboard,sound --graphics-profile quality`。
  证书忽略仅限此次用户明确授权的内网测试。
- 增量构建：`scripts_build/build_cpp_rdp_demo_probe.bat`，只构建外部 demo 目标。
  demo 构建产物和本次隔离运行副本 SHA-256 一致：
  `4F541BCA7EA06B5C26CBE7A0F644989F3FBAE40953FC3CD9D6A990575E447676`。
- 有效配置模板：`docs/examples/rdp_proxy_native_probe.ini.in`。替换路径及运行时凭证，仅保存在受限 ACL 临时目录；退出清理。
  模板现默认为 WebSocket 探针使用的回环监听；本节较早的直连 TCP 测试曾使用 `10.0.0.90:13389`。
- 本次没有修改 GammaRay Client 的运行时产物，没有向 `build_official/dist` 发布新的产品客户端。

### 验收边界和证据

- 16:48:53 经代理进入连接态，16:48:54 收到首帧；16:49:11 截图确认完整 Windows Server 桌面。
- 客户端解码日志确认 AVC444v2；proxy 使用现有 passthrough 路径，没有新增桌面采集或二次编码。
- DisplayControl 曾在 10 秒检查时报告未就绪，随后 16:49:04 报告 ready；不能把该早期提示当作最终失败。
- 16:50:58 自动重连，16:50:59 再次收到首帧；`quser` 确认仍为会话 2、登录时间 16:47。
- 键鼠自动化受到本机 Win+R 和远端快捷键焦点混用影响：已观察到远端开始菜单响应，但文本输入/组合键不计完整验收通过。
  误打开的本机 Run 对话框已经取消，未执行其中的拼接文本。
- 本轮没有完成剪贴板内容、音频实际播放、文件传输、多用户竞争、网络故障或 60 FPS 测试。
- 成功日志和桌面截图：`.cache/rdp_proxy_run_4c7d67ba/`，截图 `client-164911.png`。
- 本次连续运行约 16:43:05–16:52:16，不足 10 分钟（中途外部重启及重新部署）。
  结束确认 13389 无监听、临时防火墙规则数量为 0、任务移除；`usbtest2` 会话 2 保留为断开状态。

以下为此前失败尝试的历史记录，以上面的最终结果为准。

## 前轮日志证据（修正前）

本轮运行时间约 16:28:49–16:36:55（北京时间），不足 10 分钟。
根据用户要求，后续尝试使用 demo 已有的 `--autoconnect --cert-ignore`，不点击连接按钮、不弹证书确认。
忽略证书仅用于本次受限内网探针，不能作为产品默认配置。

| 时间 | 配置与位置 | 观察结果 |
| --- | --- | --- |
| 16:29:04 | 代理入口，Windows 原生 SSPI | `AcceptSecurityContext: SEC_E_LOGON_DENIED (0x8009030c)`；`client authentication failure` |
| 16:33:49 | 仅代理设置 `WINPR_NATIVE_SSPI=0` | `AcceptSecurityContext: SEC_E_SECPKG_NOT_FOUND (0x80090305)` |
| 16:34:52 | 客户端也设置 `WINPR_NATIVE_SSPI=0` | `InitializeSecurityContext: SEC_E_SECPKG_NOT_FOUND (0x80090305)`；`NLA begin failed`；`ERRCONNECT_AUTHENTICATION_FAILED (0x00020009)` |

16:34:52 客户端日志明确记录 `Certificate not checked, /cert:ignore in use.`。
因此最后一次失败不是证书确认阻塞，而是客户端认证上下文初始化失败。
代理随后记录的连接读取失败是客户端中止连接后的现象，不应当视为图像通道问题。
`SEC_E_SECPKG_NOT_FOUND` 尚不能单独证明密码错误，也尚未定位到具体的包查找或句柄异常分支。

原始日志保存在本机私有 ACL 的 `.cache/rdp_proxy_run_532cfe23/`：
`client/freerdp.log`、`client/rdp_client.log`、`proxy.stdout.log`、`proxy.stderr.log`。
原始认证日志可能含敏感信息，不提交、不整份公开；引用时只提取阶段、时间、错误码。

## 已处理的测试环境问题

- Windows PEM 的 CRLF 换行导致本版本 `crypto_read_pem` 按文件长度读取失败：探针生成的 PEM 统一 LF。
- INI 使用 UTF-8 无 BOM。
- 代理运行目录补齐匹配 OpenSSL 3 的 `legacy.dll`，进程环境设置 `OPENSSL_MODULES` 指向该目录。
- Qt 5 demo 的证书解析需要 OpenSSL 1.1 DLL；从现有 streamer 依赖复制到隔离的 demo 运行目录。
- 避免混用 vcpkg app-local 自动复制的 FreeRDP DLL 和本次编译产物，部署时以本次构建的五个核心 DLL 覆盖。
- demo 没有密码命令行参数：自动连接探针仅在代理入口使用与 demo 内置测试密码匹配的临时 SAM 项，后端 Windows 凭证独立传入。
  不将密码放入进程命令行、文档或提交文件。

独立构建入口：`scripts_build/build_cpp_rdp_proxy_probe.bat`。
参考源码：`D:/dolit/rdp/FreeRDP`；没有修改该第三方源树。
proxy EXE 本地与远端 SHA-256 一致：
`B4E8F1EE8F1BE173523DA247F6CAB55A90BEFF408404C268415A20CDC2809A7A`。

## 后续定位点

1. 对照 `libfreerdp/core/credssp_auth.c` 的认证函数表选择、获取凭证、初始化/接受上下文调用。
2. 核对 `winpr/libwinpr/sspi/sspi.c` 的 `WINPR_NATIVE_SSPI` 分支，以及实际装载 DLL 的路径和哈希。
3. 进一步区分 `sspi_winpr.c` 返回 `SEC_E_SECPKG_NOT_FOUND` 的具体分支：凭证句柄包名缺失，还是函数表查找失败。
4. 原生 SSPI 不接受当前测试 SAM 账号是待核实的认证实现差异；不能以反复修改 Windows 密码代替定位。
5. 认证通过后再验收 GFX 首帧、真实 codec、输入、剪贴板、声音、断开及会话复用。

## 远端访问与清理

90 可通过 WinRM Negotiate 管理，使用仓库已有的本地凭证来源；不在文档复制密码。
传统 SMB/计划任务方法见 `docs/console_render_records_view_design.md`，本次文件部署使用 WinRM 会话复制。
代理由隔离的 SYSTEM 计划任务运行，临时防火墙仅允许本机 `10.0.0.16` 访问 TCP 13389。

本轮结束已停止测试客户端和代理、恢复 demo 原设置、移除临时任务及防火墙规则，确认 13389 无监听。
远端 `quser` 仍仅显示原有 Administrator console 会话 1；没有新建用户、主动注销会话或修改原有 3389。
运行时代理配置、SAM 和私钥由探针退出清理；本地生成的临时凭证及私钥也应清理，日志留存用于定位。
