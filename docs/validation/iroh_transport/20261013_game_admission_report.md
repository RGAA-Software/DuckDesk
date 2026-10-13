# GameHook首次接入失败定位与Client收尾

本批查清声音验收407的首次接入失败，修正Client在未建立连接或已断开后继续发送hello的问题。未调整组帧、UDP发送、音视频码率、Console授权协议或会话期限。复用此前Windows/Linux网络、双Relay、小规模RDP及声音验收证据。

## 407失败的具体原因

90同一时钟上的数据库事件与Render日志顺序如下。数据库只读取会话状态及事件时间，没有读取或输出凭据；见[数据库时间线](20261013_game407_database_timeline.json)。

| 90时间 | 407事件 |
| --- | --- |
| 11:07:28.601843 | 资源会话创建 |
| 11:07:29.893938 | 连接信息生成，revision2 |
| 11:08:00.249377 | 待接入会话关闭，revision3 |
| 11:08:01.029 | Render收到接入请求并返回ADMISSION_REJECTED |
| 11:08:12.718 | 没有接入客户端，Render按原宽限规则退出 |

现有Console实现中，待接入资源会话约30秒后过期关闭。407测试夹具在获取连接信息后停留过久，业务接入到达时会话已关闭；QUIC连接建立不代表业务会话已经获准。该样本不能算媒体传输失败或音频验收通过。

408对照：11:11:59.241880生成连接信息，11:12:00.604342业务接入成功，相隔1.362462秒；同样node_generation468/control_epoch73。408的实际Relay视频及声音证据继续有效。

本机与90旧日志的时钟约相差8.7秒，不能直接按两端墙钟相减来推导拨号或丢包时间。上述407判断使用90数据库/Render同端时间线。原压缩日志保持：`.cache/audio-game-20261013/source.log.gz` SHA256 `A9A4EC22CC8A3D984687AC0BE4084E8AACB92A175EA4C3D79C87F3337D026CEF`，本机原日志同目录`local.log.gz`。

## 改动与验证

- SDK只在网络连接成功回调后允许发送hello；断开回调立即撤销就绪状态，Exit也禁止发送。正常连接上尚未收到配置时的hello重试保留，后续成功连接回调可再次发送。原iroh拨号/恢复策略不改。
- iroh最终连接失败日志明确记录`recoverable=false action=end_session`，不再把已经结束的接入尝试呈现成可持续恢复的hello失败。
- Client为ADMISSION_REJECTED增加独立类型及中英文提示：“连接请求已失效或被服务端拒绝。请重新发起连接。” 不将业务拒绝误报为网络故障或设备密码错误。具体拒绝原因不从通用错误码猜测。
- 夹具先完成构建与其它准备，随后在同一脚本中取得连接信息并立即启动Client；禁止在两步之间等待另一个人工/模型轮次。未新增短期令牌、票据或第二套认证。

`scripts_build/build_cpp_hello_lifecycle_tests.bat client 18`通过2个CTest目标、26个GTest用例。新生命周期用例使用实际本机WebSocket收发：已连接时收到两次hello，服务端断开后多次定时事件不再尝试发送；另一个用例通过真实iroh缺失端点失败回调验证未连接/最终失败后的hello抑制和终止日志。中英文目录及ADMISSION_REJECTED映射检查通过。所有权、可读命名、修改范围格式及Git空白检查通过。

新409仅一路本机Client→90 GameHook→90 Relay：连接信息到启动487.337ms，管理API观察到connected为2443.793ms；持续60.415秒后仍connected，hello发送失败0、接入拒绝0。实际路径2，音频wasapi初始化、48kHz双声道打开、首次3840字节PCM提交有日志；60.113520秒呈现3476帧，平均57.824FPS，13个超过100ms呈现间隔，最大281.563ms。本机同时进行18jobs构建，此样本只验证接入与媒体功能，不能证明无短顿、长期稳定性或推断短顿根因；不重启已暂停专项。见[单路证据与清理](20261013_game_admission_regression.json)。

409实测Client SHA为`9657B85211C317BE0C94236A54D2B9FE90F0E96E11B1102C3122B68E5D7EB460`，已包含SDK修复及终止日志。之后仅进一步区分业务拒绝的中英文UI文案，最终聚焦目录/映射回归已重跑；不把早期实测冒充最终二进制实测。最终三个产品Client均Release、显式18jobs，发布到各自开发dist并逐文件SHA核对，最终身份见本批delivery.json。Company仍为上批B7 Client，90/XR服务/Render、Console和BJ镜像均未更新；没有新Setup或Android包。

## 清理与下一步

409 owned资源closed、实例stopped、Client进程退出、guest注销；测试应用和部署恢复disabled。原应用spec逐项不变，RDP容量10、授权80保留。两个Relay仍fresh/ready、未排空，连接数90为默认Render1/BJ0；没有清理Windows账号或会话。大日志沿用压缩证据，本批新增本机gzip及SHA。

本次首次接入失败已定位，Client误导性重试及提示已修复。下一未完成交付项是汇总受影响的Windows正式候选包并做必要Setup覆盖升级，复用通过的功能和安装证据；本批focused dist不能当可安装发布包。真实复杂NAT、规模容量与长期稳定性仍按原缺口记录，Android排除，短顿/NIC/饱和混传继续暂缓。
