# GameStream 协议(Moonlight/Sunshine)分析与 DuckDesk 云游戏传输方案

> 分析对象:`D:/source/moonlight-qt`(含 moonlight-common-c 子模块)与 `D:/source/Sunshine`。
> 目的:评估为 DuckDesk 引入"专为云游戏设计的高速传输通道",替代/补充现有 WebSocket / WebRTC。

## 0. 许可证前提(必须先决策)

| 项目 | 许可证 | 能否直接用源码 |
|------|--------|----------------|
| moonlight-qt / moonlight-common-c | **GPLv3** | 用了你的整个项目就必须 GPLv3 |
| Sunshine | **GPLv3** | 同上 |
| ENet(含 Moonlight 定制 fork 依赖的原版) | **MIT** | 可以随便用 |
| GameStream 协议本身(逆向公开,有社区文档) | 无版权约束 | 可自己实现 wire 兼容 |

**结论:GPLv3 是 copyleft,"项目用宽松协议 + 抄 GPL 源码"在法律上不成立。** 两条路线:

- **路线 A**:DuckDesk 整体转 GPLv3 → 可直接链接/魔改 moonlight-common-c(客户端)和 Sunshine 的 stream/video 模块(主机端),工作量最小,还能直接获得与 Moonlight 客户端的兼容性。
- **路线 B**:保持宽松协议 → 只使用 ENet(MIT)+ 按公开协议文档自研实现,代码一行不抄;包格式对齐 GameStream 后同样能与 Moonlight 互操作。

下文的技术分析对两条路线均适用。

## 1. GameStream 协议架构总览

不同流量拆 4 条通道,端口经 RTSP SETUP 协商(Sunshine 侧为基准端口 + 固定偏移,`Sunshine/src/network.cpp:224`):

| 通道 | 传输 | 默认端口 | 职责 |
|------|------|----------|------|
| 握手 | HTTPS(nvhttp)+ RTSP over TCP(可 AES-GCM 加密 `rtspenc://`) | 47984 / 48010 | 配对、launch、能力协商 |
| 视频 | 裸 RTP over UDP,**不重传** | 47998 | H.264/HEVC/AV1 码流 |
| 控制+输入 | **ENet over UDP**(可靠 UDP) | 47999 | 控制信令 + 全部输入回传 |
| 音频 | RTP over UDP | 48000 | Opus 48kHz,5ms 包间隔 |

### 为什么比 WebRTC 快

| 维度 | WebRTC | GameStream |
|------|--------|------------|
| 拥塞控制 | GCC 动态估带宽,码率震荡 | **没有**。码率锁死协商值,发送 pacing 固定 ~800Mbps |
| 丢包恢复 | NACK 重传(尾延迟)+ 内置 jitter buffer | **不重传**:RS-FEC 纠错 → RFI 参考帧失效 → IDR 兜底 |
| 解码前缓冲 | jitter buffer 排队 | 有界队列仅 15 帧,溢出即清 + IDR |
| 编码器缓冲 | 通用参数 | **单帧 VBV**(`vbvBufferSize = bitrate/framerate`)、无限 GOP、禁 B 帧/lookahead |
| 队头阻塞 | 单连接复用 | ENet 按设备分 channel,通道间互不阻塞 |

## 2. 关键技术细节

### 2.1 视频:FEC → RFI → IDR 三级丢包恢复

- 包结构:RTP 头 + `NV_VIDEO_PACKET`(24-bit 流内包序号、帧号、SOF/EOF 标志、FEC 信息),`moonlight-common-c/src/Video.h`。
- **RS-FEC**:按帧分块(大帧最多 4 块),数据分片 N + 奇偶分片 `ceil(N*pct/100)`,默认 20% 冗余,带内下发;`reedsolomon/rs.c` GF(2^8)。客户端块齐即重建(`RtpVideoQueue.c`)。
- **投机式丢帧预测**:近期无乱序且缺口超过可用奇偶分片时,立刻判帧不可恢复并通知主机,不傻等(`RtpVideoQueue.c:213-219`)。
- **RFI(参考帧失效)**:控制通道 URGENT channel 发"帧 X~Y 失效",主机编码器跳过坏参考帧继续编 P 帧,**避免 IDR 带宽尖峰**,恢复延迟极低。NVENC 原生支持(`Sunshine/src/nvenc/nvenc_base.cpp:795`),不支持的编码器退化为 IDR。
- **IDR 兜底**:连续丢 120 帧、解码队列溢出、解码器返回 NEED_IDR 时触发。

### 2.2 控制/输入:ENet channel 隔离 + 微批处理

- channel 分配(`Limelight-internal.h:57-66`):0x00 通用、**0x01 URGENT(IDR/RFI)**、0x02 键盘、0x03 鼠标、0x10-0x1F 每手柄一个、0x20-0x2F 手柄传感器。
- 可靠性按内容分级:按键/手柄状态 reliable;陀螺仪、触摸 move **不可靠**(丢了无所谓);ping 用 reliable 是为了让 ENet ACK 持续测 RTT。
- 重传退避上限压到 2×RTT,10s 超时(`ControlStream.c:1782`)。
- **输入 1ms 微批处理**:鼠标/手柄包等 1ms 尝试与下一包合并——注释原话:批处理反而**降低**有效输入延迟,因为避免了 ENet 内部排队(`InputStream.c:54-60`)。同类合并只保留最新位置/摇杆值。
- 主机侧注入:Windows 键鼠 `SendInput`,手柄 ViGEmBus(X360/DS4,含陀螺仪校准),振动反馈经控制通道回传(`Sunshine/src/platform/windows/input.cpp`)。

### 2.3 音频

- Opus 48kHz,`RESTRICTED_LOWDELAY` + CBR;5ms 包间隔(解码慢或低码率时 10ms)。
- FEC **固定 RS(4,2)**:20ms 一块可抗任意 2 包丢失;注意 NVIDIA 校验矩阵非标准,需硬编码替换(`Sunshine/src/stream.cpp:1800-1809`)。
- 彻底丢的包喂 NULL 触发 Opus PLC;启动丢前 ~500ms 做重同步。

### 2.4 低延迟编码器参数(Sunshine,最值得直接借鉴)

- NVENC:`gopLength=无限`、`zeroReorderDelay=1`、`enableLookahead=0`、CBR、**`vbvBufferSize = bitrate/framerate`(单帧 VBV)**、`ULTRA_LOW_LATENCY`、intra-refresh 可选、按需 IDR + 参考帧失效。
- AMF:`usage=ultra-low-latency`;QSV:`async_depth=1`;x264:`zerolatency`、`keyint=-1`。
- `minimum_fps_target`:静态画面仍按最低 FPS 出 dummy 帧,防 CBR 下静态画质崩塌。

### 2.5 发送管线

- 捕获(DXGI Desktop Duplication / WGC,VRAM 零拷贝)→ 色彩转换(D3D11 shader)→ 编码 → **单线程为所有会话发包**。
- 零拷贝 shard(FEC 直接在编码输出上分片)+ `WSASendMsg` scatter-gather 批量 + 高精度定时器(`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`)按 ~800Mbps pacing。
- 捕获时间戳贯穿编码到发包,管线延迟写入帧头(1/10ms 单位),客户端可精确测延迟。
- 线程优先级:捕获 critical、编码/发送 high;Windows 请求 0.5ms 定时器分辨率。

### 2.6 握手与加密

- HTTPS `/pair`(4 阶段 PIN)→ `/launch` 客户端下发 `rikey`(AES-128 密钥)/`rikeyid`(IV 前缀)。
- RTSP:OPTIONS → DESCRIBE(SDP 能力:编码格式、RFI 支持、加密位)→ SETUP(audio/video/control,回发 ping payload / ENet connect data)→ ANNOUNCE(客户端 SDP:分辨率/FPS/码率/包大小/FEC 参数)→ PLAY。
- 加密:RTSP/控制/视频均 AES-128-GCM(IV=序号+方向字节),音频 AES-CBC。**公网必须全开**。

## 3. render 在公网:这套架构还行吗?

**结论:可行(Moonlight 官方支持互联网串流),但代价和前提要清楚。**

### 可行的原因

- **主机有公网 IP(或端口映射)时,NAT 问题天然不存在**:客户端主动向主机固定端口发 UDP ping(SS_PING),主机借此学到客户端的 NAT 映射地址并回发——客户端侧 NAT 由出站包自动打洞,即使对称 NAT 也能出得去(`VideoStream.c:55-82`,`Sunshine/src/stream.cpp:2018`)。
- 全通道有 AES-GCM 加密方案,公网可强制开启(`SS_ENC_CONTROL_V2/SS_ENC_VIDEO/SS_ENC_AUDIO`)。
- Moonlight 已针对公网做适配:远程时包大小压到 1024(IPv4)/1184(IPv6)避免分片(`Connection.c:388-411`)。

### 代价与短板

1. **没有拥塞控制是双刃剑。** 局域网带宽恒定,固定码率+pacing 是最优解;公网链路窄且波动,`最低码率==最高码率`锁死的策略意味着:**带宽够了体验极好,带宽一波动就是持续丢帧**,不会像 WebRTC 那样自动降码率保流畅。客户端只有丢帧率统计(3s 滑窗,≥30% 报 POOR)提示用户手动降码率。
   - 缓解:主机侧根据 `SS_FRAME_FEC_STATUS` 逐帧反馈动态调 FEC 百分比;码率仍需人工/半自动重协商。
2. **RTT 变大后恢复成本上升。** RFI/IDR 请求一个来回才能恢复画面;FEC 比例要调高(20% → 30-40%),进一步吃掉有效带宽。
3. **安全性要自己把关。** WebRTC 的 DTLS-SRTP 是强制且经过审计的;GameStream 的加密是自实现 AES-GCM(IV 构造正确性依赖实现),且配对/证书体系(TLS 客户端证书 + PIN)要自己部署。
4. **QoS 标记(DSCP/qWAVE)在公网基本被忽略**,局域网才有的加成没了。
5. **web 客户端永远用不了这套协议**(浏览器无裸 UDP;WebTransport 尚未普及),web 端仍必须保留 WebRTC。两套传输栈长期并存。

### 对 DuckDesk 公网场景的适配建议

- render 公网直连:客户端 UDP ping 打洞逻辑直接照搬即可,前提是主机侧端口可达(公网 IP / 端口映射 / 你们现有的 relay UDP 转发做 fallback)。
- relay UDP 转发模式下,该协议大部分优势仍在(FEC、无重传、ENet 输入),只是 pacing 速率要按 relay 带宽调。
- 保留 WebRTC 作为 web 端和弱网 fallback;新协议定位:**原生客户端 + 直连/relay UDP 场景的高速通道**。

## 4. DuckDesk 落地路径(由浅入深)

1. **第一步(纯编码侧,不动协议)**:把单帧 VBV、无限 GOP+按需 IDR、禁 B 帧/lookahead、intra-refresh 应用到现有编码链路(`src/px_render/app/encoder_thread.cpp` + AMF 插件),WebRTC 通道也能立刻受益。风险最小,先做。
2. **第二步(原生客户端专属 UDP 通道)**:rtc local 直连场景下,视频改裸 RTP + RS-FEC,控制/输入复用 ENet(可先用现有信令通道 + ENet 输入),丢包恢复先 IDR、RFI 作为增强。
3. **第三步(wire 兼容 GameStream)**:包格式对齐公开协议,换取 Moonlight 客户端直接可连,客户端侧工作量归零,且可用 Moonlight 做联调对照。

### 可直接借鉴的清单(与许可证无关的设计思想)

- ENet channel 按设备隔离队头阻塞,URGENT 独立通道传 IDR/RFI
- 输入 1ms 微批处理,只保留最新状态
- 单帧 VBV、无限 GOP、按需 IDR + RFI
- FEC 状态逐帧上报(`SS_FRAME_FEC_STATUS` 结构)驱动主机调 FEC%
- 捕获时间戳贯穿编码到发包,管线延迟写入帧头
- 有界队列溢出即清 + IDR,不做大 jitter buffer
- 发送批量化 + 高精度定时器 pacing

## 5. 关键源码索引

**moonlight-common-c**(`D:/source/moonlight-qt/moonlight-common-c/moonlight-common-c/src/`):

- `Connection.c` 分阶段连接编排;`Limelight-internal.h:57-66` ENet channel 定义
- `ControlStream.c` ENet 控制流:`sendMessageEnet()`(668)、丢帧率统计(447)、IDR 请求(1470)
- `VideoStream.c` / `VideoDepacketizer.c` / `RtpVideoQueue.c` 视频接收/组帧/FEC
- `RtpAudioQueue.c` 音频 RS(4,2);`InputStream.c` 输入批处理(323)
- `RtspConnection.c` 握手(930);`SdpGenerator.c` 客户端 SDP(码率锁定 356)
- `reedsolomon/rs.c` RS 编解码;`enet/` 定制 ENet fork

**Sunshine**(`D:/source/Sunshine/src/`):

- `stream.cpp` 发包管线:`videoBroadcastThread`(1468)、`fec::encode`(806)、pacing(1603)
- `nvhttp.cpp` 配对/launch;`rtsp.cpp` RTSP 服务(ANNOUNCE 1071)
- `video.cpp` 捕获/编码:`captureThread`(1513)、`probe_encoders`(3215)
- `nvenc/nvenc_base.cpp:255-281` 低延迟 NVENC 参数
- `platform/windows/` DXGI/WGC 捕获、SendInput/ViGEm 注入、WASAPI 音频
- `crypto.cpp` AES-GCM;`config.cpp` fec_percentage=20、ping_timeout=10s
