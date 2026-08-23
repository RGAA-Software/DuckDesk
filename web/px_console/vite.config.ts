import { fileURLToPath, URL } from 'node:url'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vueJsx from '@vitejs/plugin-vue-jsx'
import vueDevTools from 'vite-plugin-vue-devtools'
import tailwindcss from '@tailwindcss/vite'

// 本地调试：把 /api、/console(WebSocket)、/uploads、/ping 代理到运行中的 px_console_server。
// 默认 https://127.0.0.1:30500（HTTPS + 自签名证书，secure:false 忽略证书校验）。
// 端口不同时：CONSOLE_PROXY_TARGET=https://127.0.0.1:30501 npm run dev
// CMS_PROXY_TARGET is accepted for one upgrade cycle so existing developer
// environments keep working after the product rename.
const CONSOLE_PROXY_TARGET =
  process.env.CONSOLE_PROXY_TARGET ||
  process.env.CMS_PROXY_TARGET ||
  'https://127.0.0.1:30500'

// https://vite.dev/config/
export default defineConfig({
  plugins: [vue(), vueJsx(), vueDevTools(), tailwindcss()],
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },
  server: {
    host: true, // 等价于 0.0.0.0
    port: 5173,
    proxy: {
      '/api': {
        target: CONSOLE_PROXY_TARGET,
        changeOrigin: true,
        secure: false,
        configure(proxy) {
          proxy.on('proxyReq', (proxyReq, req) => {
            if (req.headers.host) proxyReq.setHeader('x-forwarded-host', req.headers.host)
          })
        },
      },
      // WebSocket 通道（/console/website 等），必须 ws:true 才能转发升级握手
      '/console': {
        target: CONSOLE_PROXY_TARGET,
        changeOrigin: true,
        secure: false,
        ws: true,
      },
      // 头像等上传资源（UserManager 里 BASE_URL + avatar_path）
      '/uploads': {
        target: CONSOLE_PROXY_TARGET,
        changeOrigin: true,
        secure: false,
      },
      '/ping': {
        target: CONSOLE_PROXY_TARGET,
        changeOrigin: true,
        secure: false,
      },
    },
  },
})
