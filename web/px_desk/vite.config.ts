import { fileURLToPath, URL } from 'node:url'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vueJsx from '@vitejs/plugin-vue-jsx'
import vueDevTools from 'vite-plugin-vue-devtools'
import tailwindcss from '@tailwindcss/vite'
import Components from 'unplugin-vue-components/vite'
import MotionResolver from 'motion-v/resolver'

// https://vite.dev/config/
export default defineConfig({
  base: '/',
  plugins: [vue(),
    vueJsx(),
    vueDevTools(),
    tailwindcss(),
    Components({
      dts: true,
      resolvers: [
        MotionResolver()
      ],
    }),],
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },
  server: {
    proxy: {
      '/api': {
        target: 'https://127.0.0.1:5001',
        changeOrigin: true,
        secure: false,
        //rewrite: (path) => path.replace(/^\/web/, '')
      },
    },
  },
})
