/// <reference types="vite/client" />

declare const __PIXELS_WEB_DISTRIBUTION__: 'development' | 'official' | 'customer'
declare const __PIXELS_WEB_DEPLOYMENT_POLICY__: string
declare const __PIXELS_WEB_DEPLOYMENT_TRUST__: string
declare const __PIXELS_WEB_CLIENT_BUILD__: number

declare module '*.vue' {
  import type { DefineComponent } from 'vue'
  const component: DefineComponent<{}, {}, any>
  export default component
}
