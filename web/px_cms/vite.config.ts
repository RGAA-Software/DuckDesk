import { fileURLToPath, URL } from 'node:url'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vueJsx from '@vitejs/plugin-vue-jsx'
import vueDevTools from 'vite-plugin-vue-devtools'
import tailwindcss from '@tailwindcss/vite'

// 本地调试：把 /api、/spvr(WebSocket)、/uploads、/ping 代理到运行中的 px_cms_server。
// 默认 https://127.0.0.1:30500（HTTPS + 自签名证书，secure:false 忽略证书校验）。
// 端口不同时：CMS_PROXY_TARGET=https://127.0.0.1:30501 npm run dev
const CMS_PROXY_TARGET = process.env.CMS_PROXY_TARGET || 'https://127.0.0.1:30500'

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
        target: CMS_PROXY_TARGET,
        changeOrigin: true,
        secure: false,
      },
      // WebSocket 通道（/spvr/website 等），必须 ws:true 才能转发升级握手
      '/spvr': {
        target: CMS_PROXY_TARGET,
        changeOrigin: true,
        secure: false,
        ws: true,
      },
      // 头像等上传资源（UserManager 里 BASE_URL + avatar_path）
      '/uploads': {
        target: CMS_PROXY_TARGET,
        changeOrigin: true,
        secure: false,
      },
      '/ping': {
        target: CMS_PROXY_TARGET,
        changeOrigin: true,
        secure: false,
      },
    },
  },
})
