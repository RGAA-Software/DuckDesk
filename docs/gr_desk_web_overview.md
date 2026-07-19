# gr_desk 站点（gr_desk_server + web/gr_desk）现状记录

> 记录时间：2026-07-18。本文档记录 GoDesk 官网站点（前端 `web/gr_desk` + 后端 `rust_server/gr_desk_server`）的架构现状，作为后续网页优化调整的基线。

## 1. 总体结构

```
用户浏览器
   │
   ▼
gr_desk_server (Rust/axum)          ← HTTP 0.0.0.0:5000 / HTTPS 0.0.0.0:5001
   ├── /api/...        → 8 个 REST 接口（issue/consult/version）
   └── 其余路径         → 静态目录 <exe>/static/（SPA fallback 到 index.html）
   │
   ▼
MongoDB (mongodb://localhost:27017, 硬编码)
   └── db_off_site: c_consult / c_issue / c_version
```

- 前端源码：`web/gr_desk/`（Vue 3 SPA），构建产物 `web/gr_desk/dist/`
- 已构建产物快照：`rust_server/web/`（index.html + assets）
- 部署：`scripts/package_gr_desk_server.bat` 将 `dist/*` 拷贝到 `output/gr_desk_server/static/`，与 `gr_desk_server.exe` 一起打包
- TLS 证书：可执行文件旁 `certs/cert.pem` / `certs/key.pem`（可用 `scripts/ensure_tls_cert.bat` 生成），缺失则启动失败

## 2. 后端：rust_server/gr_desk_server

纯 binary crate（仅 `src/main.rs`），crate 名 `gr_desk_server`，v3.2.0。

### 2.1 技术栈

- axum 0.8.1（ws + multipart）+ axum-server 0.8（rustls TLS）+ tower-http（静态文件）+ tokio
- mongodb 3.2.1（精确锁定）
- gr_base（路径依赖 `../../rust_base/gr_base`）：日志、`RespMessage<T>`/`ok_resp`、时间戳工具
- clap 4.5（CLI）、lazy_static（全局单例）

### 2.2 模块划分（每个业务域 = model + handle + manager）

| 模块 | 文件 | 职责 |
|---|---|---|
| issue | `off_issue.rs` / `off_issue_handle.rs` / `off_issue_manager.rs` / `off_issue_keys.rs` | 用户问题反馈（工单） |
| consult | `off_consult.rs` / `off_consult_handle.rs` / `off_consult_manager.rs` | 商务咨询留言 |
| version | `off_version.rs` / `off_version_handle.rs` / `off_version_manager.rs` | 产品最新版本号管理（含内存缓存） |
| 基础设施 | `off_database.rs`、`off_context.rs`（空 State）、`off_http_utils.rs`、`off_api_error.rs`（错误码 600–605）、`off_api_keys.rs` | — |
| filter | `mod.rs` | **空文件，未使用** |

全局单例（`Arc<Mutex<...>>`）：`gOffDatabase`、`gOffConsultManager`、`gOffIssueManager`、`gOffVersionManager`。

### 2.3 路由清单（全部定义在 `src/off_server.rs`，仅 8 条）

| 方法 | 路径 | 功能 |
|---|---|---|
| POST | `/api/v1/create/new/issue` | 创建问题反馈（desc 必填；title+desc+version 去重） |
| GET | `/api/v1/query/issues` | 分页查询问题列表（page/page_size，按 created_ts 排序） |
| POST | `/api/v1/mark/issue/processed` | 标记问题已处理 |
| POST | `/api/v1/create/new/consult` | 创建咨询（content/consult_type 必填；title+name+type+content 去重） |
| GET | `/api/v1/query/consults` | 分页查询咨询列表 |
| POST | `/api/v1/mark/consult/processed` | 标记咨询已处理 |
| POST | `/api/v1/update/product/version` | 更新产品最新版本号（需 verify_code） |
| GET | `/api/v1/query/product/version` | 查询最新版本号（内存缓存优先） |

统一响应：`gr_base::RespMessage<T>`。非 `/api` 路径走 `ServeDir(static).not_found_service(index.html)` SPA fallback。

### 2.4 端口与启动

- HTTP：`0.0.0.0:5000`，HTTPS：`0.0.0.0:5001`（**硬编码**，同一 router）
- CLI `-p/--port`（默认 20369）**解析后未使用（遗留 bug）**
- 日志：`logs/gr_desk_server/`，前缀 `log_off`
- MongoDB 连接失败即退出
- 2026-07-18 起：rustls 改为仅 `ring` provider（`default-features=false`，移除 aws-lc-rs C 依赖）；axum-server 改用 `tls-rustls-no-provider`（由 main.rs 显式安装 ring provider）

### 2.5 认证现状

- **无任何登录/会话/Token 机制**（依赖内网部署）
- 唯一鉴权：`update/product/version` 校验硬编码 verify_code（错误码 604）
- query / mark processed 等管理接口完全无保护

### 2.6 已知风险点

1. CLI `--port` 无效（端口硬编码 5000/5001）
2. 多处 `.unwrap()` 解析 body 字段，缺字段请求会导致 handler panic（500）
3. 管理接口无身份认证；verify_code 硬编码于源码
4. `filter/mod.rs` 空文件

## 3. 前端：web/gr_desk

GoDesk 远程桌面产品官方营销站（品牌指向 godesk.uk），package name `gammaraycm`。

### 3.1 技术栈

- Vue 3.5 + TypeScript + `<script setup>`，SPA
- Vite 6.2（`base: './'`，别名 `@` → `./src`）
- Element Plus 2.11 + Tailwind CSS 4（`@tailwindcss/vite`）+ tw-animate-css
- vue-router 4.5 / pinia 3 / vue-i18n 11（中/英双语，默认中文，localStorage 记忆）
- axios（封装于 `src/http.ts`，timeout 5s）
- 特效：cobe（WebGL 地球）、vue-use-spring、motion-v、roughjs
- 测试：Vitest + Playwright
- npm 脚本：`dev` / `build`（type-check + build）/ `test:unit` / `test:e2e` / `lint` / `format`

### 3.2 页面清单（路由定义于 `src/router/index.ts`，HTML5 history 模式）

| 路由 | 文件 | 用途 |
|---|---|---|
| `/`（布局） | `src/views/MainPage.vue` | 全站布局：顶部导航（首页/价格/文档）、语言切换、GitHub 链接、页脚（条款/隐私/联系我们/**提交工单**对话框） |
| `/main` | `src/views/HomeView.vue` | 首页：cobe 3D 地球 hero、产品卖点、截图轮播、行业方案、下载（夸克网盘 + GitHub Releases 外链） |
| `/price` | `src/views/PriceView.vue` | 价格页：个人版 0$/年 vs 企业版 69$/席位/年，功能清单 |
| `/docs` | `src/views/DocsView.vue` | 文档入口页：跳转 docs.godesk.uk，展示支持平台 |
| `/main-test` | `src/views/AboutView.vue` | **模板残留占位页（可清理）** |
| `/terms` | `src/views/TermsView.vue` | 使用条款（静态中文） |
| `/privacy` | `src/views/PrivacyTerms.vue` | 隐私政策（静态中文） |

组件（`src/components/`）：
- `ContactUs.vue` — "联系我们"咨询表单对话框（POST 后端）
- `Globe.vue` — cobe WebGL 交互地球
- `TopDecoratorDivider.vue` / `BottomDecoratorDivider.vue` — roughjs 手绘风分隔线

### 3.3 前端 API 调用（仅 2 个写接口）

| 方法 | 路径 | 调用位置 | 用途 |
|---|---|---|---|
| POST | `/api/v1/create/new/issue` | `MainPage.vue` | 提交工单 |
| POST | `/api/v1/create/new/consult` | `ContactUs.vue` | 提交商务咨询 |

- dev 环境 axios baseURL 硬编码 `https://127.0.0.1:5001`；vite dev server 另配 `/api` 代理到同一地址；prod 同源
- 前端**无读取类 API 调用**（query/issues、query/consults、query/product/version 均未被前端使用）
- `src/stores/counter.ts` 为模板残留，未使用

### 3.4 样式方案

- Element Plus 全局引入（`element-plus/dist/index.css`，size default，zIndex 3000）
- Tailwind v4 + shadcn 风格 oklch CSS 变量主题 token（`src/assets/main.css`），含 light/dark 双套（`.dark` 切换）
- ESLint 9 flat config + Prettier

### 3.5 已知小问题

1. ~~`MainPage.vue`（提交工单）与 `ContactUs.vue`（咨询表单）中 `qq` 字段误传 `wechat` 值~~ **已于 2026-07-18 视觉改版中修复**
2. ~~`/main-test`（AboutView）和 `counter.ts` 为脚手架残留~~ **已于改版中删除**
3. 查询类后端接口前端完全未用（无管理界面；构建产物标题为 "GoDesk CM" 暗示曾有管理端规划）

### 3.6 2026-07-18 视觉交互改版记录

- 风格：全站改为深色科技感主题（`index.html` 固定 `dark`），品牌蓝 `#2979ff`，Element Plus 引入官方暗色变量 `element-plus/theme-chalk/dark/css-vars.css`
- 响应式：全面改造（flex/grid + 断点），移动端含抽屉菜单；删除全部固定像素布局
- 动效：新增 `src/directives/reveal.ts`（v-reveal 指令，IntersectionObserver 入场动画，支持 prefers-reduced-motion）；导航吸顶滚动毛玻璃；卡片悬浮提升
- i18n：`src/locales/zh.ts` / `en.ts` 全量重写，所有页面正文接入（Terms/Privacy 正文保持中文原文）
- 结构变化：
  - 新增 `src/components/FeatureSection.vue`（首页 5 个卖点区块统一组件）
  - 新增 `src/components/DotGlobe.vue`（自研 Canvas 2D 点阵地球：陆地采样 + 自转 + 拖拽 + 城市脉冲 + 飞线弧光，替代原 cobe WebGL 地球——cobe 地球在部分环境贴图渲染失败呈纯黑球，且其浅色配置不适配深色主题）
  - 新增 `src/directives/reveal.ts`、`src/assets/earth-map.ts`（256x128 陆地采样贴图 data URI，提取自 cobe，MIT）
  - 删除 `AboutView.vue`、`stores/counter.ts`、`TopDecoratorDivider.vue`、`BottomDecoratorDivider.vue`、`Globe.vue`（后改名 TheGlobe 再被 DotGlobe 替代）及 `/main-test` 路由
  - 移除依赖：`roughjs`、`cobe`、`vue-use-spring`
  - `src/http.ts` 删除未用的 `getHostPort`/`HOST_PORT` 与 `X-Custom-Header`
- 表单：工单/咨询表单接入 el-form rules 必填校验（咨询含邮箱格式校验），提交 loading + 成功后重置；修复 `qq` 字段传值 bug
- 首页截图轮播改用 ResizeObserver 按 16:9 动态计算高度
- 导航：弃用 el-menu 改为自定义 Tailwind 导航（深色适配 + 吸顶毛玻璃 + 移动端 el-drawer 抽屉）；移除点击无反应的 Steam 图标

## 3.7 生产部署（腾讯云 43.134.55.209）

- 部署目录：`/root/off_site/`（二进制 + `static/` + `certs/` + `logs/`），root 用户 `setsid nohup` 运行，无 systemd
- **公网链路（重要）**：nginx 443 server 块以 `/var/godesk.uk` 为静态根直接伺服前端，`location /api` 反代到 `127.0.0.1:5000`；`/root/off_site/static` 仅供直连 IP:5000/5001（被安全组拦截，实际无公网流量）。**前端发布必须更新 `/var/godesk.uk`**
- `docs.godesk.uk` 由 nginx 伺服 `/var/site`，与本服务无关
- `/var/godesk.uk/document/` 为历史文档目录，部署时必须保留
- nginx 证书由 certbot 自动续期
- 一键部署脚本：`rust_server/scripts/deploy_desk_server.sh`
  - `build_desk_server_musl.sh`：zig + cargo-zigbuild 交叉编译 musl 静态二进制（复用 `.tooling/`，含 libudev stub）
  - 构建前端 `web/gr_desk` → scp 二进制与 static.tar.gz → 备份旧二进制（`*.bak.时间戳`）→ pkill 旧进程 → nohup 启动 → 验证端口与 HTTPS 200
  - SSH 免交互：`SSH_ASKPASS` + `StrictHostKeyChecking=accept-new`，密码读自 `gr_desk_server/tencent_server.txt`
- 2026-07-18 首次部署：旧进程 `gr_off_site`（139 天）已替换为 `gr_desk_server`，旧二进制保留备份

### 3.8 2026-07-19 赛博风格改版（gopico AURIS Green V6）

- 设计蓝本：`D:\source\GoPhone\gopico\design\gopico_design.html` 的 `auris-green-v6` 段 + `gopico-pc-ui/src/ui/theme.rs`
- 视觉体系：近黑深绿灰底（#070908/#111411/#151915）+ 荧光绿 #18d875 单一强调色 + 45° 切角（面板 14px/按钮输入框 9px，clip-path，全局无圆角）+ 等宽字体（Consolas/Cascadia Mono）大写 HUD 排版
- 关键 CSS 技巧（`src/assets/main.css`）：`.cyber-panel` 双层伪元素切角描边、`.el-button/.el-input` 渐变拼边框切角（gopico 设计稿原文）、`.cyber-title` 绿竖条标题、`.kpi-num/.kpi-line`、`.cyber-corners` L 形角标、`.bg-hud-grid`
- Element Plus 全量赛博化覆盖（按钮/对话框/输入框/下拉/通知/抽屉/轮播指示器），`--el-color-primary: #18d875`
- DotGlobe 改绿色系；logo PNG 用 CSS filter hue-rotate 调绿
- 首页新增 KPI 数据条（8K/144/4:4:4/双网）、终端风状态行 + 光标闪烁
- 注意：`--accent-foreground` 在 :root 中若被重复定义会导致绿底绿字（曾踩坑，最终定义为 #06110a）

### 3.9 2026-07-19 多产品门户改版

- 站点从单品官网升级为产品家族门户：`/main` 门户主页（品牌 Hero + 产品矩阵 + 亮点带），产品详情页 `/products/godesk`（绿 #18d875）、`/products/goxr`（青 #2ac7c4）、`/products/cybermonitor`（紫 #7548d8）
- 产品：GoDesk 远程桌面（现内容迁移）、GoXR Manager（gopico 套件：PC 管理端 + PICO Agent + Android 瘦终端，VR→Android 同屏）、CyberMonitor（CyberDesktop 套件资源监控，Client/Host 远程集中监控）
- 主题色机制：产品页根节点设 `--pa` CSS 变量，组件（ProductHero/ProductSection/FeatureGrid/ProductCard）用 `var(--pa)` 取色
- 新增组件：`ProductHero.vue`、`ProductSection.vue`、`FeatureGrid.vue`、`ProductCard.vue`；导航增加“产品▾”下拉（各带主题色方块）
- 素材：logo 取自源项目（gopico-pc.png / ic_cyber_monitor.svg），产品界面图用 Playwright 截两个项目的高保真设计稿（gopico_design.html / cyber_monitor.html）存于 `src/assets/products/`
- **重要修复**：vite `base` 由 `'./'` 改为 `'/'`——相对 base 在二级路由（/products/*）下资源路径解析错误导致白屏
- GoXR 下载入口为商务咨询（无公开渠道）；CyberMonitor 指向 GitHub RGAA-Software/CyberDesktop

### 3.10 2026-07-19 蓝色赛博主题 + 价格三分页

- 主题色从荧光绿改回 GoDesk 品牌蓝：token 改名 `--green*` → `--brand*`（`--brand:#2f8fff / hover #58abff / dark #1e6fd6 / dim #132a45`），背景改蓝灰调暗色；产品主题色不变（GoXR 青 / CyberMonitor 紫），GoDesk 产品色 #2f8fff
- 按钮文字/图标一致性：`.el-button--primary` 白字（`--accent-foreground:#f2f8ff`），按钮内图标统一 CSS filter（primary 白、默认 88% 亮）
- 弹窗完整边框：`.el-dialog` 弃用 border+clip-path（角部缺边），改双层伪元素切角描边
- Hero 恢复覆盖式（标题压在点阵地球上，蓝色系）
- 价格页改三个产品分页（自定义赛博 tab 切换器）：GoDesk 原定价 / GoXR（定制 UI 免费 + ¥1000/设备授权）/ CyberMonitor（完全免费 ¥0 + GitHub）

### 3.11 2026-07-19 管理后台（/admin）

- **安全修复**：query/mark 接口此前公网裸奔（任何人可拉取全部留言含联系方式），已加鉴权
- 鉴权：exe 旁 `desk_settings.toml`（`[admin] password`，首次启动自动生成随机密码落盘，不进 git）；请求头 `X-Admin-Token`；`POST /api/v1/admin/verify` 登录校验；未授权返回 606
- 后端新增：`off_settings.rs`（配置加载/生成）、`off_admin_handle.rs`（verify + check_admin_token）；query handlers 支持可选 `processed` 过滤；create handler 的 serde `.unwrap()` 改为 600（修复恶意请求 panic）
- 前端：`/admin`（AdminLogin，sessionStorage 存密码）+ `/admin/panel`（AdminPanel：咨询/工单 cyber-tab、状态筛选、赛博表格、内容展开、标记已处理/恢复、翻页）；`src/adminHttp.ts`（自动带 token，606 跳登录；dev 走 vite 代理避免 CORS）
- 管理密码仅存于服务器 `/root/off_site/desk_settings.toml`

## 4. 优化调整可考虑的方向（备忘）

- 网页内容/视觉/交互优化（本次重点，待明确具体需求）
- 清理脚手架残留（AboutView、counter.ts、`/main-test` 路由）
- 修复 `qq` 字段传值 bug
- 后端：管理接口鉴权、`--port` 生效、body 解析 `.unwrap()` 防御
- 是否需要利用 query/product/version 接口在页面上动态展示最新版本号
