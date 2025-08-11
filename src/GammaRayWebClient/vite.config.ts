import { fileURLToPath, URL } from 'node:url'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vueJsx from '@vitejs/plugin-vue-jsx'
import vueDevTools from 'vite-plugin-vue-devtools'
import basicSsl from '@vitejs/plugin-basic-ssl'
import typescript from '@rollup/plugin-typescript';
import transformer from '@libmedia/cheap/build/transformer';

// https://vite.dev/config/
export default defineConfig({
    server: {
        host: "0.0.0.0",
        https: false,
        proxy: {
            // 代理所有以/api开头的请求
            '/api': {
                //target: 'http://192.168.31.5:20371', // 你的服务器地址
                target: 'http://10.0.0.16:20371', // 你的服务器地址
                changeOrigin: true,
                rewrite: (path) => path.replace(/^\/api/, '') // 可选，重写路径
            }
        }
    },
    plugins: [
        vue(),
        vueJsx(),
        vueDevTools(),
        basicSsl(),
        typescript({
            // 配置使用的 tsconfig.json 配置文件
            // include 中需要包含要处理的文件
            tsconfig: './tsconfig.json',
            transformers: {
            before: [{
                type: 'program',
                factory: (program) => {
                    return transformer.before(program)
                }
            }
            ]},
        })
    ],
    resolve: {
        alias: {
            '@': fileURLToPath(new URL('./src', import.meta.url)),
        },
    },
})

// npm install @libmedia/common
// npm install @libmedia/cheap
// npm install @libmedia/avformat
// npm install @libmedia/avcodec
// npm install @libmedia/audioresample
// npm install @libmedia/audiostretchpitch
// npm install @libmedia/videoscale
// npm install @libmedia/avnetwork
// npm install @libmedia/avprotocol
// npm install @libmedia/avrender
// npm install @libmedia/avpipeline
// npm install @libmedia/avplayer
// npm install @libmedia/avplayer-ui
// npm install @libmedia/avtranscoder
//
