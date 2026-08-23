# GoDesk 远程控制设计评审报告

> 评审范围:`src/`(panel / render / client 三进程)、`rust_client/`、`rust_base/protocol`。
> 已剔除 `docs/crash-risk-analysis.md`(35 项崩溃风险)与 `docs/codebase-analysis/` 已覆盖的内容,以下均为新发现。
> 评审角度:远程控制软件设计(信任模型、连接生命周期、线程模型、守护逻辑、协议健壮性)。

## 总体判断

当前系统的信任模型是"**认证靠客户端自觉、保密靠网络可达性**":被控端(render)不验证任何凭证,中继链路明文,面板把设备密码通过无鉴权 HTTP 广播到局域网,Console 的"密钥"可从公开算法推导。四项叠加后,"设备码+密码"的产品形态在实际攻击面下基本不成立。

稳定性方面,最突出的是三类确定性 bug:**断线/拥塞场景的无退出条件自旋死循环**(发送线程永久卡死)、**跨线程无保护共享状态**(编码器 map、Qt GUI 调用)、**守护进程逻辑缺陷**(停止无效、拉起风暴只修了一半)。

---

## 一、安全与信任模型(最高优先级)

### S1【高】被控端对连接零鉴权,密码校验只在主控端"自觉"执行
- `src/px_render/plugins/net_ws/ws_server.cpp:283-346`:`/media`、`/file/transfer` 只要求 `stream_id` 非空即开始推流;`net_relay/relay_plugin.cpp:143-155` 中继通道同样收到 `RequestControl` 直接推流。
- `plugin_net_event_router.cpp:121-315`:消息分发入口无鉴权门,键鼠注入、文件传输、`kStopRender`(杀进程,306 行还缺 `break` 落入锁屏分支)、`kLockDevice` 全部直接受理。
- 密码校验 `/verify/security/password`(`net_ws/http_handler.cpp:46`)只是"校验预言机",调用发生在**主控面板侧**——修改过的客户端或裸协议可跳过密码直接看屏、注入键鼠、读写文件。
- 修复:被控侧强制完成基于密码的挑战-应答握手,通过前不注册路由、不处理任何消息。

### S2【高】面板本地 HTTP 服务绑定 0.0.0.0 零鉴权,明文吐出设备 ID+随机密码,可杀任意进程
- `src/px_panel/src/render_panel/network/ws_panel_server.cpp:245` 绑 0.0.0.0,TLS 整段注释。
- `/simple/info` → `px_context.cpp:261-264` 返回 `did` + `rpwd`(随机密码明文);`/kill/process` 按 pid 杀任意进程(`http_handler.cpp:143-159`);`/game/start` 按客户端给定路径启动程序。
- 同网段任何人(或恶意网页 DNS rebinding)一个请求即得远控凭据,构成 LAN 内 RCE。
- 修复:绑定 127.0.0.1;敏感接口加配对 token;密码永不出接口。

### S3【高】Rust 服务 WS 监听 0.0.0.0 零鉴权,`StartServer.app_path` 任意指定 → 本地提权 SYSTEM
- `rust_client/px_service/service_core/src/config.rs:8`(`DEFAULT_LISTEN_HOST = "0.0.0.0"`)、`websocket_server.rs:45-55`(仅校验 path);`windows_process.rs:103-126` 用服务自身 SYSTEM token 拉起传入路径的 exe。
- 叠加:持久化启动参数存 `C:\Users\Public\GoDesk\px_data\godesk_service.json`(`windows_util.rs:7-20`,任何用户可写),服务按它拉起 SYSTEM 进程。
- 修复:绑定 127.0.0.1 + 校验 app_path 必须在安装目录内;数据/日志迁 ProgramData 并收紧 ACL。

### S4【高】中继链路明文、无 E2E 加密;Console app_secret 可从 appkey 确定性推导
- `relay_plugin.cpp:121,248` 均 `ssl_ = false`,屏幕/键盘(含用户键入的密码)/剪贴板/文件明文过中继;中继被攻陷即可主动控制任意在线设备。
- `src/px_client/network/ct_auth_token.cpp:29-42`:`app_secret = MD5(SHA256(appkey + 硬编码盐))`,盐随客户端分发 → Console 管理接口实际零鉴权,`/query/device/by/id` 返回设备明文随机密码,`/update/random/pwd` 可重置任意设备密码。
- 修复:中继只做密文转发(E2E 密钥由设备密码派生);app_secret 改为授权服务器下发的真随机秘密;管理接口独立鉴权,响应删除明文密码。

### S5【中高】凭据处理全线薄弱
- 安全密码经命令行明文传给 render(`px_render_controller.cpp:114-115`)且全量落日志;校验协议是"客户端发 MD5、服务端比 MD5"——MD5 即密码等价物;校验接口无频控,空密码即放行(`http_handler.cpp:62-66`)。
- 远端设备密码明文存本地 SQLite(`app_stream_list.cpp:500-503,552`)。
- 剪贴板文本全文落日志(`clipboard_manager.cpp:50-53`)。
- `src/px_render/private.key` + `certificate.pem` 私钥入库(旧版遗留,需确认无在网部署使用,建议删除+轮换+清洗历史);`AES_DEPLOY_AUTH` 硬编码,分发信息"加密"只是混淆。
- 修复:密码改 WS 通道下发、日志脱敏、DB 用 DPAPI 加密、挑战-响应替代 MD5 比对。

### S6【高】剪贴板文件读取 = 未鉴权任意文件读
- `src/px_render/plugins/clipboard/clipboard_plugin.cpp:132-162`:`OnRequestFileBuffer` 对客户端报来的任意路径直接读文件回传,无"该文件是否在共享集合内"的白名单。叠加 S1,远程可读 render 进程权限内任意文件。
- 文件传输"授权目录"限制是死代码:`file_operate.cc:23` `s_file_permission_path_ = "/"` 从无人赋值,白名单检查永不执行,`Remove()` 对任意路径 `remove_all`。

---

## 二、确定性 Bug(稳定性/正确性)

### B1【高】断线/拥塞场景多处无退出条件的自旋死循环
- RTC 背压:`rtc_plugin.cpp:106-138` `while (queuing > 256 || !has_buffer) DelayBySleep(1)` 无超时无存活检查,DataChannel 死亡后发送线程永久卡死。
- 客户端 SDK WS/relay 分支:`sdk_net_client.cpp:296-309,336-343` 同样忙等且无 `IsChannelReady()` 检查;`DelayByCount` 是 QPC 自旋。键盘事件在 UI 线程直发(`ct_video_widget.cpp:205-207`)→ 上行拥塞时**整个 UI 冻结**。
- 修复:循环内加存活检查+超时,失败丢消息;发送线程化,UI 永不阻塞。

### B2【高】输入事件丢失/乱序,远端按键鼠标卡死
- 断线时鼠标事件队列(容量 64)满即**静默丢弃最旧**(`thread.cpp:108-114`),release 事件可丢,重连后无"全量释放/状态重同步"→ 远端鼠标粘连、按键卡死。
- 键盘(UI 线程直发)与鼠标(异步队列)两条路径,顺序无保证,"点击输入框→打字"字符可进错窗口。
- 修复:按类型区分丢弃策略(move 可丢,press/release/key 不可丢);键鼠统一有序通道;重连后发 reset-input。

### B3【高】px_render 三处线程/生命周期 bug
- DDA 错误回调在采集线程同步执行,切 GDI 时 `join` 自身线程 → `std::terminate`(`dda_capture.cpp:380` → `rd_app.cpp:189-207` → `thread.cpp:176-184`)。
- `encoder_plugins_` map 跨线程无锁读写(`encoder_thread.cpp:316,384` vs `rd_statistics.cpp:144`、`plugin_net_event_router.cpp:486`),重建编码器与统计并发可迭代器失效。
- `capture_plugin_` 成员未初始化(`rd_app.h:165`),inner/mock 模式下野指针被解引用(`rd_app.cpp:418-424,769-779,1031`)。
- 另:`ReleaseAllPlugins()` 在 px_render 无调用者,退出路径从不停插件/采集;编码格式状态全局单值导致多屏 codec 不一致(`encoder_thread.cpp:134`);编码器全初始化失败无退避每帧重试且落选实例 session 泄漏(`:236-313`)。

### B4【高】RGB 帧缓冲只按首帧大小分配,分辨率增大时 `memcpy` 堆溢出
- `src/px_client/front_render/ct_video_widget.cpp:293-299`:I420/I444 路径都有尺寸变化重分配,唯独 RGB 路径漏掉。远端切到更大分辨率显示器即触发。

### B5【高】Rust 服务守护逻辑缺陷
- **StopDesktop 无效**:`service_host.rs:165` 只杀进程不清 `last_desktop_launch`,3 秒后 monitor 自动拉回(`state.rs:56`),停止等于重启。
- **拉起无退避**:`service_host.rs:272-309` 每 3s 巡检立即拉起,无冷却无失败计数——26ea0c5 只修了 user proxy 的 panel 分支,服务侧 render/user proxy、keepalive 的 SysInfo 分支(`keepalive.rs:199-205`)都是同款风暴。
- 每次心跳全量 WMI `Win32_Process` 枚举且在全局锁内(`windows_process.rs:64-77`),WmiPrvSe CPU 常驻偏高。
- 服务声明接受 SHUTDOWN 却不处理、STOP 不报 STOP_PENDING → 关机/停止留孤儿进程,user proxy 还会继续拉起 panel("服务已停、进程自愈")。
- SCM 失败动作只配了第一次重启(`manager.rs:115-125`),连续崩两次服务永久停摆。

### B6【中】协议层缺陷
- 两端 SDP 解析失败前就调 `SetRemoteDescription`(`rtc_server.cpp:205-209`、`rtc_connection.cpp:261-263`),空指针直接传给 WebRTC。
- RtcServer 资源创建失败 `exit(EXIT_FAILURE)` 杀进程,空 factory 继续解引用(`rtc_server.cpp:177-198`)。
- 文件传输接收端缺包无超时,缓存超 8192 静默 `clear()`(标注 TODO 未上报)→ 文件静默损坏(`rtc_data_channel.cpp:102-143`)。
- 文件下载不校验回包:对端推送任意 `task_id` + `target_file_path` 即可在控制端任意路径写文件(`file_transmit_sdk.cc:356-388`)。
- 全链路无协议版本协商,proto3 未知枚举静默落到 0(恰好是合法消息类型,如 `kConsoleClientHello = 0`)→ 新旧版本混跑产生静默错误行为。
- WS 插件背压接口恒 `return true`(`ws_plugin.cpp:152-158`),节流在 WS 通道失效;`pending_data_count_` 指标恒 0,基于它的阈值全部失效。

### B7【中】NAT 打洞无降级路径
- 唯一 ICE server 是硬编码 IP `stun:39.91.109.105:60498`(单点),TURN 被 `if(0)` 永久关闭(账号还是 test/123456);ICE 失败无超时、不通知 SDK 回退 relay → 对称 NAT/禁 UDP 企业网下"连接成功后黑屏"。

### B8【中】客户端其他
- 音频 PCM 队列无上限,延迟单调累积;回调未填满时尾部不清零出杂音(`ct_audio_player.cpp:47-69`)。
- 解码线程直接调 Qt GUI API 与 D3D11 swapchain(`ct_workspace.cpp:134` → `ct_d3d11_video_widget.cpp:53-78`),`m_NeedsResize` 非原子跨线程读写[存疑但违反 Qt 规则]。
- 滚轮用节流后的陈旧光标位置,水平滚动分量被垂直覆盖(`ct_video_widget.cpp:125-142`)。
- `SDLVideoWidget::RefreshImage` I420 分支无限自递归(`ct_sdl_video_widget.cpp:67-70`,藏在 TEST_SDL 宏后的地雷)。
- 四层 asio 客户端均固定 1s 无退避无限重连,服务器恢复瞬间 thundering herd。
- 文件上传失败处理失效:发送失败外层循环不退出;限速令牌桶整数除法恒 0(`file_transmit_sdk.cc:628-730`)。

---

## 三、设计层面缺失(产品/企业能力)

- **无主机侧连接确认/通知**(被控人不知道谁在看)、无隐私屏、无水印(代码已注释,`ct_game_view.cpp:482-503`)、无双人授权;审计只覆盖连接/断开/文件传输,键鼠/剪贴板/锁屏/杀进程无审计。
- 空安全密码不强制随机密码兜底,不符合"未设密码设备不可无人值守访问"的行业惯例。
- 心跳每 1s 一次且每次回包 LOGI(`ct_console_client.cpp:42,163-165`),日志无意义膨胀。
- render 以 SYSTEM 跑用户会话(捕获 UAC 所需,行业常见),但放大了 S3 的后果——若不需要操作安全桌面,应改用 WTS 用户 token。

## 建议修复顺序

1. **堵攻击链**:S1(被控侧强制握手)→ S3(服务绑 127.0.0.1 + app_path 白名单)→ S2(面板绑 127.0.0.1 + 移除密码外发)→ S6(剪贴板白名单)。
2. **修确定性挂死/崩溃**:B1(自旋死循环)→ B4(堆溢出)→ B3(线程 bug)。
3. **守护正确性**:B5(stop 无效 + 全面退避)。
4. **体验类**:B2(输入状态同步)→ B8(音频/滚轮)。
5. **架构债**:S4(E2E 加密、真随机 secret)→ B7(TURN/降级)→ B6(协议版本)→ 企业能力(确认/水印/审计)。
