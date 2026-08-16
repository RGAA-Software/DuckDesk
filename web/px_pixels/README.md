# PIXELS 官网

企业云渲染解决方案 · 云游戏 / 云渲染 / 云桌面 / 远程桌面技术服务商官网。

## 技术栈

- Vue 3 + TypeScript + Vite
- Ant Design Vue 4（主题色 `#00b96b`）
- vue-i18n（简体中文 / English）
- 纯 SVG 像素风视觉（Logo / 点阵字标 / 图标 / 插画），无图片依赖

## 开发

```bash
npm install
npm run dev     # 本地预览 http://localhost:5173
npm run build   # 类型检查 + 生产构建
npm run preview # 预览生产构建
```

## 说明

- 单页锚点滚动结构：Hero / 技术服务 / 行业应用 / 合作模式 / 技术优势 / 服务流程 / 合作方案 / 常见问题 / 联系
- 品牌标识：像素云朵图形 + PIXELS 点阵字标（`PixelLogo.vue` + `PixelsWord.vue`）
- 文案在 `src/locales/zh-CN.ts` 与 `src/locales/en-US.ts`；图标、锚点、数值等结构数据在 `src/data/content.ts`
- 联系方式、地址、备案号、统计数字为占位符，正式上线前请替换
