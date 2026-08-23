# 语音通话 M2 核心音频测试报告（2026-08-23）

## 范围与结论

本轮验证 Windows M2 候选的核心音频链：WASAPI 默认通信设备、48 kHz 单声道 10 ms 回调、WebRTC APM（AEC/NS/AGC）、20 ms Opus、60 ms 抖动预填充、200 ms 有界队列、PLC、麦克风/扬声器静音以及并发停止。

结论为“核心链与显式设备选择已实现，并完成真实设备冒烟及2小时真实WASAPI闭环长稳”，不是 M2 完整发布验收。双物理终端外放回声、USB/蓝牙热插拔和弱网音质仍未完成。

## 自动化结果

| 项目 | 结果 |
| --- | --- |
| `test_voice_call` | 33项：31通过；普通本机构建中2项真实WASAPI/长稳条件测试按设计跳过。 |
| `test_client_voice_call_protocol` | 5/5通过。 |
| `test_client_virtual_display` | 11/11通过。 |
| CTest | 3/3通过，0失败；最终候选连续复测通过。 |
| 当前候选60秒闭环 | 1/1通过；编码3120、解码3117、抖动峰值3包、PLC 0、采集/播放drop 0、APM失败0、设备失败0；Private Bytes下降20480字节，句柄224→225。 |

新增覆盖包括：64包媒体重放窗口、乱序/重复/回绕、抖动缓冲容量和丢包、APM 10 ms正反向处理、dummy端到端编解码播放、静音期间传输保持及恢复、并发Stop幂等、SDL/WASAPI设备枚举和显式端点选择，以及设备重路由/APM重建/致命事件只通知一次。

## 90号机真实WASAPI

- 环境：10.0.0.90，Windows交互Console会话，默认通信采集/播放端点均为Active。
- 初始结果：播放端探针成功；采集端因该用户Windows麦克风总开关为`Deny`返回`Permission denied`。这是确定的系统隐私拒绝，产品应返回`no_mic`，不得显示假连接。
- 受控复测：记录原值后临时设为`Allow`，运行真实WASAPI双工测试；采集和播放回调均大于0。最终候选以显式选择的非默认麦克风端点运行693 ms通过。`finally`恢复原值并复核为`Deny`，计划任务已删除。
- 2小时长稳：2026-08-23 14:22:08–16:22:12运行7200秒，GTest实际7203.986秒，1/1通过、退出码0、无脚本异常；期间每5分钟覆盖一次麦克风/扬声器静音与恢复，音频传输未连续停滞10秒，内部包数、抖动峰值、Private Bytes增长不超过64MiB及句柄增长不超过32的断言全部通过。测试前麦克风隐私值为`Deny`，结束后恢复并复核为`Deny`。
- 长稳证据：`tests/artifacts/voice_call/3.3.53-20260823/longevity/r90/`保存结果JSON、GTest XML、完整日志和执行脚本。该长稳使用的测试程序SHA-256为`6DE59D5EF94FB5B8F30BAAFA11784B2B96EBD94A0CA1F539DF480DDC6014097D`，APM SHA-256为`8E1142C95A5E70499708CFED87DACA879B0A71421848A688D7AEEB4FAC2E4A3E`。
- 候选部署：`voice_call.dll`与`px_voice_apm.dll`已部署到90号机；`px_service`运行，Render/Panel处于交互会话，20369/20371监听正常，Render进程已实际加载两个模块。

## 90号机 WebClient 与安装包

- Chrome 正常 `http://10.0.0.90:20371` 入口按浏览器安全上下文规则禁用麦克风，页面明确提示需要 HTTPS/localhost，sender为空且未创建本地麦克风轨道。
- 使用标记为仅测试的安全源开关连接正式 Render/Panel 后，Panel 真实拒绝返回 `rejected`，30秒无人操作返回 `timeout`，两者都停止并解绑麦克风。
- Panel 真实接受后，浏览器麦克风上行 RTP 从172增至11842字节，第二条专用语音下行从276增至7451字节；麦克风静音/恢复、通话扬声器独立静音/恢复、挂断释放和下一次通话重新显示安全提示均通过。测试开关只用于隔离当前 HTTP 部署限制，不代表生产 HTTPS 已验收。
- 接受测试期间，90号机交互用户麦克风隐私值仅临时从`Deny`改为`Allow`，完成后恢复并复核为`Deny`。E2E证据中的连接参数已脱敏，受控桌面在截图前被遮盖。
- 最终 `Pixels_3.3.53_Setup.exe` 以交互管理员上下文静默覆盖安装，安装器退出码0且远端实算哈希匹配。安装后 Service/Panel/Render 均响应，Panel与Render在Session 1、Service在Session 0，20369/20371/20375监听正常；USBMMIDD `ROOT\DISPLAY\0004` 状态OK、Amyuni 2.0.0.1、驱动签名有效，中英文双方安全提示及卸载登记均存在。安装器自身当前为`NotSigned`，仍是正式发布前必须补齐的发布项。
- 证据：`tests/artifacts/voice_call/3.3.53-20260823/web_e2e/`和`tests/artifacts/voice_call/3.3.53-20260823/install/r90/`。
- 验收结束后已删除90号机本轮全部测试计划任务、临时安装包、连接令牌、UI截图和探针脚本；保留已安装产品和USBMMIDD。清理后Panel/Render仍在Session 1、Service仍在Session 0，20369/20371对测试网络可达；20375按设计仅监听本机回环。

## 候选产物哈希（SHA-256）

| 产物 | SHA-256 |
| --- | --- |
| `test_voice_call.exe` | `F34982B3AB8EEF1304F659E0D4838D988D1EED5A3C68F86F0B639627366C7BDC` |
| `px_voice_apm.dll` | `8E1142C95A5E70499708CFED87DACA879B0A71421848A688D7AEEB4FAC2E4A3E` |
| `voice_call.dll` | `6073C8A7CEB11A12B8684D72AAB1C24BC327DB18C4A4DA0424CC071B6D0C6A4E` |
| `px_client.exe` | `E4FF2885BDF7F996A6E50D6817DAA61621F90388710938D6026D94172E5EE368` |
| `Pixels_3.3.53_Setup.exe` | `743C076EBC1A6D9AC911AE48072C4FABE0DEDE80BDB7B2D43BC8D65A9EAD0014` |

## 未通过本报告宣称完成的门禁

- 双物理终端耳机与外放各10分钟、ERLE/双讲/NS/AGC主客观指标。
- 默认设备切换、USB拔插、蓝牙profile、休眠恢复和连续切换10次。
- 视频、系统声音、文件传输并发长稳；本次2小时为核心音频真实端点闭环，不替代双物理机通话长稳。
- 原生UI真实点击设备选择及两个静音按钮后的双端听感验证。
- 生产 HTTPS、Edge、真实浏览器麦克风异常、安装升级/卸载的完整发布矩阵。
