# Console Web 前端开发调试指南（web/px_console）

> 记录时间：2026-08。本文档说明 Console 管理后台前端（`web/px_console`，Vue 3 + Vite）的两种使用方式：日常开发用 `npm run dev` 热更新调试，正式发布用 `build_console_web.bat` 编译部署。

## 1. 端口与后端结构

`px_console_server` 只监听一个端口：

| 端口 | 协议 | 功能 |
|------|------|------|
| **30500**（`console_port`） | HTTPS + WSS | 完整服务：所有 `/api/v1/*`、WebSocket（`/console/*`）、静态资源（`/assets` `/uploads` 等）、`/ping` 健康检查、SPA fallback |

- 浏览器访问后台走 `https://<ip>:30500`。随包证书是自签名证书，首次访问允许浏览器继续即可。
- 存活探针：`curl -k https://localhost:30500/ping`。
- 后端 CORS 策略是「拒绝所有跨域」，因此浏览器跨端口直连 API 会被拦截，开发模式必须走 Vite 代理。

## 2. 日常开发（`npm run dev` + HMR，推荐）

### 2.1 启动步骤

1. 确保后端在跑：
   ```
   output\px_console\px_console.exe --running-mode=server
   ```
2. 启动前端 dev server：
   ```
   cd web\px_console
   npm run dev
   ```
3. 浏览器打开 `http://localhost:5173/`，登录即可。
4. 修改 `.vue` / `.ts` 文件 → Vite 自动热更新（HMR），无需再编译部署。

### 2.2 代理机制（已配好，无需手动改）

开发模式下前端请求全部走 Vite 代理（同源，绕开 CORS 与自签名证书问题）：

- `vite.config.ts` 的 `server.proxy`：
  - `/api`、`/uploads`、`/ping` → `https://127.0.0.1:30500`（`secure:false` 忽略自签名证书）
  - `/console` → 同上，且 `ws:true`（转发 WebSocket 升级握手）
- `src/http.ts` 在 `import.meta.env.DEV` 下：
  - `BASE_URL` 返回 `''`（相对路径，走代理）
  - `HOST_PORT` 返回 `window.location.host`（WebSocket 也走代理）

> 代理目标端口默认 30500；如后端改了端口，用环境变量覆盖：
> `CONSOLE_PROXY_TARGET=https://127.0.0.1:30501 npm run dev`

## 3. 正式编译部署（`build_console_web.bat`）

只改前端、需要产出可部署产物时，在仓库根目录执行：

```
.\build_console_web.bat
```

脚本做两件事（不重编 Rust 服务端）：

1. `web\px_console` → `npm ci` + `npm run build`（`vue-tsc` 类型检查 + `vite build`）
2. 清空旧产物后，把 `dist\*` 拷贝到 `output\px_console\web\`（`px_console_server` 从 exe 旁的 `web\` 目录提供静态文件）

部署后如 `px_console_server` 正在运行，重启它才会加载新前端资源。

> 如需连 Rust 服务端一起编译，用 `build_px_console_server.bat`；首次完整部署（证书/配置/运行时目录）用 `scripts\package_px_console_server.bat`。
