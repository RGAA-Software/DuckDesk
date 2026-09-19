# px_web_client

浏览器端 WebRTC Direct Host 远程桌面客户端(Vue 3 + TypeScript + Vite + Element Plus)。Console 与 Render 启动描述符都只允许 `rtc_direct`；SDP 直接与目标 Render 交换，不使用中心 Relay 信令、STUN、TURN 或降级路径。

## 功能

- 填写设备 ID、安全密码,通过 WebRTC 直连被控端(render)。流 ID 由设备 ID 自动派生(`web_<deviceId>`,只读),同一台设备同时只允许一路连接。
- 新连接遇到设备已被占用时(render 返回 code 704),页面提示"是否接管",确认后带 `takeover=1` 重新发信令,顶掉旧连接。
- 支持 URL query 参数带入:`/?deviceId=xxx&password=zzz`(或 `pwd_md5=`),带入后仍可手动修改。
- 信令流程:创建 `RTCPeerConnection`(不配置 iceServers)→ 创建 label 为 `Pixels` 的 datachannel → `createOffer` → `setLocalDescription` → 等待 ICE gathering complete(不使用 trickle)→ POST 到同源 `/alloc/local/rtc` → 收到 `answer_sdp` 后 `setRemoteDescription`。
- 远端视频流全屏显示;datachannel 的 onopen/onmessage/onclose 打印日志(为后续控制消息预留)。
- 状态展示:未连接 / 连接中 / 已连接 / 失败(含错误原因),失败后可重新连接。

## 信令契约

```
POST /alloc/local/rtc   (与页面同源,render 端 4601 端口)
请求: {"offer_sdp": "...", "device_id": "...", "stream_id": "...", "password": "..."}
响应: {"answer_sdp": "..."}
```

## 开发

```bash
npm install
npm run dev
```

开发服务器端口 5174,已配置把 `/alloc` 代理到 `http://127.0.0.1:4601`(本地 render 端),如需修改见 `vite.config.ts`。

## 构建

```bash
npm run build
```

普通 `npm run build` 只生成 development 产物并输出到 `dist/`。正式 Official/Customer Web Client 不允许单独手工拼装，必须由
Cloud Node 或 Remote 的完整产品矩阵构建生成；矩阵构建会把该发行对应的 deployment policy、approved trust store 和当前产品 build
水位注入 Web bundle。缺少任何一项，或 policy 与发行类别不一致时，构建失败关闭。`vite.config.ts` 中 `base: './'` 为相对路径，产物可部署
到 Render 的 `/web/` 子路径。

## 部署身份门禁

Console 生成的启动 URL fragment 必须携带 `console_origin`、资源会话 ID、revision 和一次性 frontend token。Official/Customer bundle 在创建
`RTCPeerConnection`、向 Render 发送 token 或使用任何凭据前，先跨源访问 Console 的公开身份端点，验证 `PXDC1` 证书、`PXDD1` 短期描述、
`PXDP1` nonce 持有证明、发行类别、协议/build 水位和本地持久化单调水位。Official 只接受编译时固定的官方 HTTPS origin 与 deployment ID；
Customer 只接受签名类别为 `private` 的部署，并在首次成功后按 Console origin 固定 deployment ID。验证失败时不会回落到手工设备密码路径。

development bundle 保留本地手工连接入口用于聚焦开发，不构成 Official/Customer 产品行为。

## 部署

Render 在同源 `/web/` 路径下托管本前端，RTC 信令仍走 Render 同源相对路径 `/alloc/local/rtc`。部署身份发现和 nonce proof 访问启动描述符指定的
Console HTTPS origin；Console 仅对这两个不含账号凭据、且内容经过签名的公开端点开放 GET/POST CORS，不扩大其他 Console API 的跨域权限。
