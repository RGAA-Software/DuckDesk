# WebRTC 局域网直连 Web 客户端(px_web_client)

> 状态:已可用(2026-08)。浏览器 WebRTC 直连 render,支持画面、键鼠、悬浮控制、文件传输。
> 本文取代/补充 `06-web-client.md` 中旧 React 原型的描述(旧 `src/px_web_client` 已弃用)。

## 架构

```
浏览器(Vue3, web/px_web_client)
  │  ① 同源 HTTP 信令  POST /alloc/local/rtc (render net_ws, 20371)
  │     请求体 {"sdp": offer}, query 带 device_id/stream_id/safety_pwd_md5
  │     响应 {"code":200,"data":{"answer_sdp": ...}}
  │     —— render 端无 STUN/TURN,gathering 瞬间完成;
  │        AddCandidateIpToAnswer 把 candidate 改写为客户端看到的地址
  ▼
RTCPeerConnection(无 iceServers,局域网/公网直连)
  ├─ video/audio track      ← 桌面画面(H264,经 RtcSharedVideoEncoder 复用主编码管线产物)
  ├─ media_data_channel     ← 键鼠输入、控制消息(CAD/锁屏/刷新/切显示器/改帧率)
  └─ ft_data_channel        ← 文件传输(列目录/上传/下载)
页面托管:render net_ws 的 /web_client/(与信令同源,无 CORS/混合内容问题)
入口:Console 设备列表 "Web桌面" 按钮按 desktop_link 拼 http://{ip}:{rdpt}/web_client/?deviceId=...
```

## datachannel 协议

- 每条二进制消息 = **32 字节 NetTlvHeader(小端,含 4 字节对齐填充)+ tc.Message 序列化**
- 整包 `type_=1`;>128KB 时 render 按 Begin/Center/End 分片,接收端按 `this_buffer_begin_` 重组
- ft 通道 `pkt_index_` 严格递增(render 按它排序投递)
- proto:`px_message.proto`(键鼠/控制)+ `px_file_transfer.proto`(文件传输)

## render 侧关键点(net_rtc_local)

- `POST /alloc/local/rtc` 需 `safety_pwd_md5`(设备安全密码 MD5,未设密码则放行),错误返回 403
- 视频:`RtcSharedVideoEncoder` 是"搬运工",从插件缓存按采集序号取主编码管线编好的帧;
  编码慢于采集时回退取最新缓存帧、delta 静默丢弃;首帧前只发 key 帧;
  建连时主动 `InsertIdr` + 清旧缓存(首帧 <3s)
- 帧缓存 `encoded_video_frames_` 跨线程访问,有互斥锁保护(曾因此崩溃,勿去锁)
- UDP 端口范围 60430-60490(安装时已加防火墙放行规则)
- 鉴权/黑白名单:`/alloc/local/rtc` 仅密码校验,无网段限制(局域网部署前提)

## 首帧优化(2026-08)

三处改动使首帧从 ~16s 降到 ~2.6s:
1. `rtc_server.cpp` 建连(track 添加后)即 `InsertIdr` + `SetClearOlderFramesBaseline(now)`
2. `rtc_video_encoder.cpp` `mWaitIDRFrame`:本 peer 未发出关键帧前 delta 一律丢
3. 编码序号连续性以编码帧自身序号为准(webrtc frame.id 与采集序号不严格一致)

## web 客户端(web/px_web_client)

- Vue 3 + vite + element-plus,单页:连接表单(URL 参数 `deviceId/streamId/password/pwd_md5` 带入,齐全自动连接)
- 键鼠:鼠标 ratio 换算(object-fit:contain 黑边剔除)、VK 映射表、移动 30ms 节流
- 悬浮工具条:本地(全屏/声音/仅观看/文件传输)+ 远程(CAD/刷新/切显示器/锁屏/帧率/重启 render)
- 文件传输:列目录、上传(64KB 块 + 滑动窗口 + 水位控制)、下载(逐块 ack + Blob 落盘)
- 构建:`npm run build` → dist,经 `scripts/collect_dist.py` 收进安装包 `web_client/`

## 调试

- Render Local RTC 日志：`C:/Users/Public/Pixels/px_logs/px_render_rtc.dll.log`
- 无头验证:Chrome `--remote-debugging-port=9222` + CDP(node),video 元素 `videoWidth>0` 即出流
