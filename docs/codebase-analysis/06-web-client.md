# 浏览器客户端 `gr_web_client` 详解

## 1. 技术栈

`src/gr_web_client` 是一个独立前端工程，技术栈包括：

- React 18
- TypeScript
- Vite 6
- protobufjs
- axios
- `@libmedia/*` 解码/渲染库

这说明它不是简单网页壳，而是带浏览器端媒体解码能力的专门客户端。

## 2. 前端入口结构

当前核心文件关系可以概括成：

```text
index.tsx
  -> App.tsx
  -> gr_app.ts
      -> client/gr_sdk.ts
          -> gr_ws_conn.ts / gr_rtc_direct_conn.ts
      -> renderer/gr_renderer_manager.ts
      -> messages/gr_proto_processor.ts
```

这条线很清楚：

- `GrApp` 负责页面和连接初始化
- `GrSdk` 负责选择连接实现
- `GrConn` 子类负责收包
- `GrProtoProcessor` 负责协议解码
- `GrRendererManager` 负责视频解码与绘制

## 3. `GrApp` 的职责

`gr_app.ts` 做的事情不多，但很关键：

1. 清空页面默认边距
2. 读取 URL 查询参数
   - `host`
   - `connType`
3. 找到 `canvas` 和 `video` DOM
4. 创建 `GrRendererManager`
5. 根据 `connType` 选择 SDK 连接类型
6. 启动 `GrSdk`

当前支持的连接类型至少有：

- `ws`
- `rtc_direct`
- `rtc`

其中：

- `ws` 使用 canvas 渲染
- `rtc_direct` / `rtc` 使用 `<video>` 元素显示远端媒体流

## 4. `GrSdk` 是连接分发器

`GrSdk` 当前主要做两件事：

- 根据 `GrSdkConnType` 选择连接实现
- 把连接对象和渲染器绑定起来

当前可见实现有：

- `GrWsConn`
- `GrRtcDirectConn`

整体上它更像一个浏览器端的轻量会话门面。

## 5. WebSocket 模式

`GrWsConn` 的行为很直接：

1. 构造 `ws://host:20371/media?...`
2. 建立 WebSocket
3. 接收二进制消息
4. 统一转成 `Uint8Array`
5. 交给上层 `parseMessage`

这里的 URL 结构和原生客户端拼的 `/media?...` 非常接近，说明浏览器端和原生客户端消费的是同一类服务接口。

## 6. WebRTC Direct 模式

`GrRtcDirectConn` 负责：

1. 创建 `RTCPeerConnection`
2. 创建 data channel
3. 创建 offer
4. 调用 `/api/alloc/local/rtc` 交换 SDP
5. 设置 answer SDP
6. 在 `ontrack` 中把远端视频流绑定到 `<video>`

从实现看，这条链路目前还偏原型/实验性质，原因有两个：

- 设备 ID、stream ID 里还能看到硬编码示例值
- 逻辑比 WebSocket 简化很多

但架构方向很明确：浏览器端未来不只走帧流，也会走 RTC 媒体轨道。

## 7. 浏览器端协议处理

`GrProtoProcessor` 目前逻辑非常聚焦：

- 用 `protobufjs` 解码 `GrProtoMsg.Message`
- 判断消息类型
- 当前主要处理 `kVideoFrame`

这说明浏览器端目前仍以“收视频并显示”为主，控制面能力明显弱于原生客户端。

## 8. 浏览器端渲染与解码链

`GrRendererManager` 是最核心的实现之一。它大致流程是：

1. 收到 `videoFrame`
2. 遇到首个关键帧时初始化解码器
3. 初始化 WebGL 渲染器
4. 编译并加载 wasm 解码资源
5. 用 `WasmVideoDecoder` 解码 H.264/H.265 帧
6. 根据帧宽高和窗口尺寸调整 viewport
7. 把 AVFrame 渲染到 canvas

### 这里的关键点

- 浏览器端不是依赖浏览器默认视频解码标签，而是显式走 wasm 解码路径
- 首要渲染后端是 WebGL
- 会根据窗口大小动态计算显示尺寸

这是一条“自控解码 + 自控渲染”路线。

## 9. 工程构建与开发环境

`vite.config.ts` 暴露了几个重要信号：

- 构建目标是 `esnext`
- dev server 监听 `0.0.0.0`
- `/api` 代理到本地开发期服务
- 设置了 `COOP` / `COEP`

设置跨源隔离头意味着当前前端依赖一些更底层的媒体/wasm能力，这和 `@libmedia` 的使用是一致的。

## 10. 浏览器端的当前定位

从代码成熟度看，`gr_web_client` 当前更像：

- 轻量观看/接入客户端
- 协议验证场
- 浏览器媒体链路实验场

它已经具备独立连接、解码和渲染能力，但功能广度还明显弱于原生客户端：

- 客户端插件缺失
- 文件传输能力未见完整接入
- 控制面逻辑很轻
- RTC 流程仍较初级

