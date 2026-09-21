import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { escapeHtml, resolveWebBuildBranding } from './build_branding'

const distribution = process.env.PIXELS_WEB_DISTRIBUTION ?? 'development'
if (!['development', 'official', 'customer', 'oem'].includes(distribution)) {
  throw new Error('PIXELS_WEB_DISTRIBUTION must be development, official, customer, or oem')
}

function releaseIdentityResource(environmentName: string): string {
  const path = process.env[environmentName]
  if (!path) throw new Error(`${environmentName} is required for a non-development Web Client build`)
  return readFileSync(path, 'utf8')
}

let deploymentPolicy = ''
let deploymentTrustStore = ''
let clientBuild = '0'
if (distribution !== 'development') {
  deploymentPolicy = releaseIdentityResource('PIXELS_WEB_DEPLOYMENT_POLICY_FILE')
  deploymentTrustStore = releaseIdentityResource('PIXELS_WEB_DEPLOYMENT_TRUST_FILE')
  clientBuild = process.env.PIXELS_WEB_CLIENT_BUILD ?? ''
  if (!/^[1-9][0-9]*$/.test(clientBuild)) {
    throw new Error('PIXELS_WEB_CLIENT_BUILD must be a positive integer for a non-development Web Client build')
  }
  const policy = JSON.parse(deploymentPolicy) as { distribution?: unknown }
  if (policy.distribution !== distribution) {
    throw new Error('The Web Client deployment policy does not match PIXELS_WEB_DISTRIBUTION')
  }
  JSON.parse(deploymentTrustStore)
}
const branding = resolveWebBuildBranding(
  distribution,
  process.env,
  fileURLToPath(new URL('./src/assets/px_icon.png', import.meta.url)),
  readFileSync,
)

// https://vite.dev/config/
export default defineConfig({
  plugins: [
    vue(),
    {
      name: 'pixels-release-brand',
      transformIndexHtml(html) {
        const titleMarker = '<title>Pixels</title>'
        if (!html.includes(titleMarker)) {
          throw new Error('Web Client index title marker is missing')
        }
        return html.replace(titleMarker, `<title>${escapeHtml(branding.applicationName)}</title>`)
      },
    },
  ],
  define: {
    __PIXELS_WEB_DISTRIBUTION__: JSON.stringify(distribution),
    __PIXELS_WEB_DEPLOYMENT_POLICY__: JSON.stringify(deploymentPolicy),
    __PIXELS_WEB_DEPLOYMENT_TRUST__: JSON.stringify(deploymentTrustStore),
    __PIXELS_WEB_CLIENT_BUILD__: clientBuild,
    __PIXELS_WEB_APPLICATION_NAME__: JSON.stringify(branding.applicationName),
    __PIXELS_WEB_ICON_DATA_URL__: JSON.stringify(branding.iconDataUrl),
    __PIXELS_WEB_OEM_PROFILE_SHA256__: JSON.stringify(branding.oemProfileSha256),
  },
  // 部署在 render 端的 /web/ 路径下,使用相对 base 保证资源可加载
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
        target: 'http://127.0.0.1:4601',
        changeOrigin: true,
      },
      '/get': {
        target: 'http://127.0.0.1:4601',
        changeOrigin: true,
      },
    },
  },
})
