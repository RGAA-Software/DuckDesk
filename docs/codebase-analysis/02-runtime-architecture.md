# 运行时架构总览

## 1. 系统由哪些进程组成

从当前代码和构建图看，系统运行时至少包含下面几个角色：

### `GammaRay`

桌面控制面板。负责本机配置、设备管理、启动/停止渲染进程、展示状态、管理用户和安全设置。

### `GammaRayRender`

被控端渲染/采集进程。负责抓取桌面或应用画面、编码、网络传输、文件传输、剪贴板、输入事件回放等。

### `GammaRayClientInner`

原生远控客户端。负责连接远端流、解码显示、播放音频、输入回传、客户端插件能力。

### `GammaRayWebClient`

浏览器端轻量客户端。当前看主要支持：

- WebSocket 收视频帧
- WebRTC direct 模式
- 浏览器端解码与渲染

### 辅助进程/组件

- `GammaRayService`
- `GammaRayServiceManager`
- `GammaRayGuard`
- `panel_companion.dll`
- `tc_protection.dll`

这些组件分别承担服务管理、守护、私有扩展逻辑和保护能力。

## 2. 主链路怎么跑起来

一个典型链路可以概括成：

1. 用户启动 `GammaRay`
2. 面板初始化配置、日志、单实例锁、语言、托盘、设备信息
3. 面板通过 `GrRenderController` 启动 `GammaRayRender`
4. `GammaRayRender` 读取 `settings.toml` 和参数，加载插件
5. 渲染端启动采集、编码、网络输出、面板回传、文件传输等能力
6. 远端客户端或 Web 客户端连接到渲染端
7. 渲染端持续输出视频、音频、控制消息
8. 客户端显示画面并把输入/控制消息再发回渲染端

## 3. 进程间关系图

```text
+------------------+
| GammaRay 面板    |
| 配置/设备/控制   |
+---------+--------+
          |
          | 启动/控制/查询
          v
+------------------+
| GammaRayRender   |
| 采集/编码/传输   |
+---+----------+---+
    |          |
    |          |
    |          +-----------------------------+
    |                                        |
    v                                        v
+------------------+                 +------------------+
| GammaRayClient   |                 | WebClient        |
| 原生客户端       |                 | 浏览器客户端     |
+------------------+                 +------------------+
```

## 4. 面板和渲染端之间不是简单父子关系

虽然面板负责拉起渲染端，但两者不是“函数调用式”的强耦合关系，而是通过多种机制协作：

- 命令行参数
- 本地 HTTP API
- WebSocket
- 进程间消息
- 共享配置/本地数据库
- 插件事件

从 `render_panel/network/render_api.cpp` 可以看到，面板会主动访问渲染端 HTTP 接口，例如：

- `/verify/security/password`
- `/get/render/configuration`

这说明渲染端本身暴露了一层可被面板消费的控制 API。

## 5. 插件是运行时的第一等公民

这套系统不是把所有功能硬编码进主进程，而是把大量关键能力下沉成动态插件：

### 渲染端插件

- 采集
- 编码
- 网络发送
- 文件传输
- 剪贴板
- 中继
- 本地 RTC
- SSL 代理
- 帧处理

### 客户端插件

- 屏幕录制
- 剪贴板
- 文件传输

主进程只负责：

- 扫描插件目录
- 加载 DLL
- 调用统一接口
- 注入上下文参数
- 订阅事件回调

这意味着很多功能边界都不在 `main.cpp` 或 `workspace` 里，而在插件接口和事件路由里。

## 6. 配置分散在三层

### 构建期配置

通过 CMake 开关决定编译哪些能力：

- `BUILD_PREMIUM`
- `TC_PROTECTION_ENABLED`
- `MIMALLOC_ENABLED`
- `STAMP_LABEL_ON_DIRECT`

### 运行期静态配置

- `settings.toml`
- 插件 `.toml`
- 皮肤 `skin_config.toml`

### 运行期动态配置

- 本地 `SharedPreference`
- 数据库记录
- 面板/渲染端通过消息同步的状态

因此排查问题时，不能只看一个配置文件。

## 7. 当前活跃代码与非活跃代码的区分

当前真正活跃的交付路径是：

- `src/GammaRay`
- 顶层 Premium 插件和皮肤
- `src/GammaRayWebClient`

而以下路径虽然还在仓库里，但不属于当前主运行时：

- `src/GammaRayServer`
- `src/backup`
- `src/GammaRay/deps/**` 中的大量第三方源码

分析和维护时应该优先盯住前者，否则会被历史代码干扰。

