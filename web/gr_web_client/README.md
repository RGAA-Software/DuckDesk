# gr_web_client

浏览器端 WebRTC 直连远程桌面客户端(Vue 3 + TypeScript + Vite + Element Plus)。

## 功能

- 填写设备 ID、流 ID、安全密码,通过 WebRTC 直连被控端(render)。
- 支持 URL query 参数带入:`/?deviceId=xxx&streamId=yyy&password=zzz`,带入后仍可手动修改。
- 信令流程:创建 `RTCPeerConnection`(不配置 iceServers)→ 创建 label 为 `godesk` 的 datachannel → `createOffer` → `setLocalDescription` → 等待 ICE gathering complete(不使用 trickle)→ POST 到同源 `/alloc/local/rtc` → 收到 `answer_sdp` 后 `setRemoteDescription`。
- 远端视频流全屏显示;datachannel 的 onopen/onmessage/onclose 打印日志(为后续控制消息预留)。
- 状态展示:未连接 / 连接中 / 已连接 / 失败(含错误原因),失败后可重新连接。

## 信令契约

```
POST /alloc/local/rtc   (与页面同源,render 端 20371 端口)
请求: {"offer_sdp": "...", "device_id": "...", "stream_id": "...", "password": "..."}
响应: {"answer_sdp": "..."}
```

## 开发

```bash
npm install
npm run dev
```

开发服务器端口 5174,已配置把 `/alloc` 代理到 `http://127.0.0.1:20371`(本地 render 端),如需修改见 `vite.config.ts`。

## 构建

```bash
npm run build
```

产物输出到 `dist/`。`vite.config.ts` 中 `base: './'` 为相对路径,可直接部署到任意子路径。

## 部署

render 端在同源 `/web_client/` 路径下托管本前端。将 `dist/` 内容交给后端打包/拷贝到对应静态目录即可,信令请求走同源相对路径 `/alloc/local/rtc`,无需额外配置跨域。
