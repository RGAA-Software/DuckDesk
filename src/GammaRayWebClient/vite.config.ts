
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import typescript from '@rollup/plugin-typescript';
import transformer from '@libmedia/cheap/build/transformer';

// https://vitejs.dev/config/
export default defineConfig({
  build: {
    target: 'esnext',
  },
  server: {
    host: "0.0.0.0",
    https: false,
    proxy: {
      // 代理所有以/api开头的请求
      '/api': {
        // target: 'http://192.168.31.5:20371', // 你的服务器地址
        target: 'http://10.0.0.16:20371', // 你的服务器地址
        changeOrigin: true,
        rewrite: (path) => path.replace(/^\/api/, '') // 可选，重写路径
      }
    },
    headers: {
        // 跨域相关头
        'Access-Control-Allow-Origin': '*', // 允许所有域，或指定特定域如 'https://example.com'
        'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, PATCH, OPTIONS',
        'Access-Control-Allow-Headers': 'X-Requested-With, Content-Type, Authorization',
        
        // 多线程相关头
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp'
    },
  },
  plugins: [
    react(),
    typescript({
      tsconfig: './tsconfig.json',
      compilerOptions: {
        outDir: 'dist/'
      },
      transformers: {
        before: [
          {
            type: 'program',
            factory: (program) => {
              return transformer.before(program);
            }
          }
        ]
      }
    }),
  ],

  worker: {
    plugins: () => {
      return [
        typescript({
          transformers: {
            before: [
              {
                type: 'program',
                factory: (program) => {
                  return transformer.before(program);
                }
              }
            ]
          }
        }),
      ]
    }
  }

  }
);
