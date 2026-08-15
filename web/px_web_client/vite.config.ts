import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// https://vite.dev/config/
export default defineConfig({
  plugins: [vue()],
  // 部署在 render 端的 /web_client/ 路径下,使用相对 base 保证资源可加载
  base: './',
  build: {
    outDir: 'dist',
  },
  server: {
    host: true,
    port: 5174,
    // 开发时把信令请求代理到本地 render 端
    proxy: {
      '/alloc': {
        target: 'http://127.0.0.1:20371',
        changeOrigin: true,
      },
      '/get': {
        target: 'http://127.0.0.1:20371',
        changeOrigin: true,
      },
    },
  },
})
