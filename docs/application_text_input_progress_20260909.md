# 跨客户端文本输入：实施与剩余验收

更新：2026-09-10。远端 90 离线，按用户要求暂停实机验收，继续完成本地开发与构建收尾。

## 已完成基线

- 收尾提交 `1129e4173` 已推送 `origin/master`：WebView 光标、文本剪贴板和相关验收记录。
- 光标用户实测通过；中文剪贴板往返通过。剪贴板通过不等于输入法完成。
- 设计入口：[跨客户端文本输入设计](application_text_input_design_20260909.md)。

## 新功能实施状态

90 当前离线。用户要求先完成本地开发，再暂停等待远端上线；不执行远端 GUI 测试，
不为了验收主动注销用户、重启机器或停止无关程序。**中文输入远端验收尚未通过。**

以下实现已完成本轮本机增量构建和自动化回归；这不代表远端实机验收通过：

- Qt：`application_text_input.*`、`application_text_input_gate.h`，本机编辑面板、输入仲裁和协议收发。
- Web：面板已接入 App/FloatBall；本机 IME 组合状态、草稿、UTF-8 校验、回执和输入屏障已集成。
  普通英文输入保留旧 textarea 路径，面板编辑及屏障等待时解绑，防止重复发送。
- Render：`ingress/application_text_service.*` 实现协议工作流、租约/目标/代次检查及提交协调；
  CEF 和 Hook 适配器分别负责具体提交，不经剪贴板或全局输入回退。
- game：`hook_capture/win/hk_obs/game_text_input.*` 与测试覆盖自有目标文本适配；
  进程准入仍必须是本次私有 Job 成员 AND 规范化完整路径匹配。
- 协议 610–615，普通输入代次字段 616；旧 580 语义不变。
  `px_message.proto` 为唯一来源；CMake 生成轻量 `message_type_ids.h`，RTC 权限/消息分类使用
  `px::wire` 命名枚举，避免重复维护协议数字或引入完整 protobuf/Abseil 依赖。
- Web 截至本次记录 49 项单测通过，类型检查和生产构建通过；包含状态机、协议、
  普通输入隔离、断线不确定性和 game 不透明目标标识测试。
- 本轮 Qt/Render/Hook、权限矩阵和进程所有权共 9 组本机 CTest 全部通过，详见下方收尾记录。

## 最新载体决定：不增加连接

用户明确要求只扩展现有连接上的消息，覆盖早期额外 WS/辅助绑定方案：

| 客户端 | 实际复用载体 | 不新增的内容 |
|---|---|---|
| Qt | 现有已认证 WS/WSS 控制连接 | 连接、路由、控制者、票据交换 |
| Web | 现有可靠有序 `media_data_channel`，原有 protobuf + TLV | DataChannel、WebSocket、路由、票据交换 |

Web 显式要求 `ordered=true`，并核对 `maxRetransmits` 与 `maxPacketLifeTime` 均为 null。
不可靠 `input_data_channel` 继续承担原有普通输入，不承载新整段提交；信令 WS 不挪作应用控制。
复用既有应用实例/控制租约授权，不凭相同 stream_id 放行，也不创建第二个控制占用。

编辑 begin/end 屏障回执分配明确的 input_generation，过滤旧不可靠通道迟到事件。
屏障或提交结果不确定时保留草稿、不自动重发；无法确认控制代次时继续阻断普通输入，
提示重新连接，而非恢复旧代次。`submitted` 只表示后端接受接口调用，必须另看远端实际文字。

被替代的 WebSocket 尝试已保存真实未提交内容与清单：
`backup/application_text_websocket_retirement_20260909_2345/`，仅供参考，不参与构建/测试/运行。

## Web 本地发布记录

`web/px_web_client/dist` 已同步到 `build_official/dist/web_client`，5 个资源 SHA-256 与相对文件集合一致。
此记录不代表离线的 90 已更新；原生 Client/Render 的发布哈希见下方本机收尾记录。

| 资源 | SHA-256 |
|---|---|
| `index.html` | `CA1635C850F292327F03E8D76E5633D4A1131B407DD269F8681CB2ABCA993AFC` |
| `assets/index-9GllcpNb.js` | `1D5EDBD4E588E136C95E0576BEBFB75AB39BF2143AEA51DB3184CAA15E157B66` |
| `assets/index-CjSrVPTx.css` | `EC19690A8EB7A0DF983048520E5B4CB0CAB280385890C78033F39B2070306BE3` |

两个图片资源内容相同，SHA-256 为 `D368282210F0F6A66E72ECD119A8504524DF9D81FA7667A19366F595055BB770`。
最新替换前 Web 运行产物保留在 `.cache/web-client-before-game-target-20260909-2350`，没有删除。

## 剩余验收（不得以代码或单测替代实机）

1. 两客户端在 WebView/game 的本机 IME、选区替换、emoji、多行、断线和失权。
2. WebView 右键剪贴板、剪切/撤销/重做、同实例重连状态、动态画面和音频。
3. RDP 文件传输真实断网中断、32 MB 完整文件、覆盖/跳过。
4. A/B 普通用户配置和应用状态重启持久性：需另行协调重启，不主动注销或重启 90。
5. 60 FPS/音画同步、麦克风、多显示器、打印机；无实物时标记待验收。
6. Android/iOS 实机和指定目标游戏：设备/应用尚待确认，不宣称全覆盖。

每个交互测试批次最多 10 分钟，仅操作本次启动的实例；发布运行产物后必须核对 SHA-256。

## 90 上线后的中文输入验收入口

| 客户端/目标 | 真实系统 IME | 远端文字和选区 | 当前结果 |
|---|---|---|---|
| Qt → WebView | 待测 | 待测 | 未验收，90 离线 |
| Web → WebView | 待测 | 待测 | 未验收，90 离线 |
| Qt → 自有 game | 待测 | 待测 | 未验收，需指定游戏及 90 上线 |
| Web → 自有 game | 待测 | 待测 | 未验收，需指定游戏及 90 上线 |
| Android/iOS Web | 待测 | 待测 | 未验收，设备待确认 |

先确认用户已提供可用机器/目标且允许该批交互，再核验运行产物版本。记录真实输入法候选框、
W/Space/Enter/Ctrl 不泄漏、英文恢复、反复开关、目标改变、回执丢失和重连结果；
不把模拟 composition、接口 accepted/submitted 或协议单测视为真实中文输入已通过。

## 2026-09-10 本机收尾记录

- 使用 `scripts_build/build_cpp_tests.bat` 增量构建 `px_render`、`px_client`、`px_gh` 和以下测试目标，全部成功。
  未执行 release-only 的 `build_official.bat`，未连接、更新或操作离线的 90。
- CTest：`client_application_text_input`、`application_text_protocol`、`application_text_service`、`game_text_input`、
  `rtc_payload_authorization`、`logical_session_registry`、`ws_ipc_client_lifecycle`、`game_owned_process`、`game_process_identity`
  共 9 组全部通过；整批 14.91 秒，每项超时 45 秒。
- Web `npm run test:unit`：8 个文件、49 项通过；本次重跑用时 2.20 秒。
- 本轮回归修复了输入框销毁后误接管父窗口、排队后撤权仍可执行的问题；
  撤权后恢复权限也不能重新授权旧请求，重新取得控制权会生成新输入能力代次。
  该代次与文件传输控制租约分离；能力撤销事件仅释放旧控制者记录的按键状态。
- 文本回执沿请求所在的既有控制通道返回，不广播到 media UDP。
  相关消息分类使用具名枚举；protobuf wire-type 的 0/1/2/5 是字段编码类型，不是业务消息编号。
- `check_cpp_ownership` 构建目标、暂存区所有权检查、`git diff --check` 和新增 C++ 文件格式检查通过。
- 下列本机运行文件已同步到 `build_official/dist`，逐项源文件/目标文件 SHA-256 一致。
  发布时没有停止无关程序；Web 5 个资源的相对文件集合与哈希也已重新核对。

| 运行文件 | SHA-256 |
|---|---|
| `px_client.exe` | `314BD6DB6E4BB50E2F6E9B3D12FE948197390264B2320997B3942151610A20DF` |
| `px_render.exe` | `564C2855B98734A24E4F715F4E3C96B828482046DE7180ACD7E66545ABFFF50A` |
| `px_gh.dll` | `169F9EC2FB9BECA05DEABEC1166DEEFAB61A838456680D45FE52E4D01567BE28` |
| `px_render_rtc.dll` | `5D94228227D2F084985519D834BD8AF18DA5BB42A11E7F37920F7318D0FC7EAD` |
| `px_render_rtc_remote.dll` | `A6EAB430F6FC37D5BE25BF0989CA4743C3791B9567C1B96FE13EB0E22ED7FF3D` |

第一版边界：每个输入代次最多缓存 4096 个提交结果，达到上限后返回 busy，需重新建立主控制会话。
屏障释放失败或结果不确定时，不以关闭/重开输入面板解除保护，也不自动重发文字。
Hook 的 Win32 隐藏窗口测试不是 UE/Unity 或用户指定游戏兼容性证明；CEF 提交接口接受也不是 DOM 文本变化证明。
本次整理提交后暂停开发与测试，等待 90 上线再执行上方实机验收矩阵。
