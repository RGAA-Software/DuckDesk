import { fileURLToPath, URL } from 'node:url'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vueJsx from '@vitejs/plugin-vue-jsx'
import vueDevTools from 'vite-plugin-vue-devtools'
import basicSsl from '@vitejs/plugin-basic-ssl'

// https://vite.dev/config/
export default defineConfig({
    server: {
        host: "0.0.0.0",
        https: false,
        proxy: {
            // 代理所有以/api开头的请求
            '/api': {
                target: 'http://192.168.31.5:20371', // 你的服务器地址
                changeOrigin: true,
                rewrite: (path) => path.replace(/^\/api/, '') // 可选，重写路径
            }
        }
    },
    plugins: [vue(), vueJsx(), vueDevTools(), basicSsl()],
    resolve: {
        alias: {
            '@': fileURLToPath(new URL('./src', import.meta.url)),
        },
    },
})
