/// <reference types="vite/client" />

declare const __PIXELS_WEB_DISTRIBUTION__: 'development' | 'official' | 'customer' | 'oem'
declare const __PIXELS_WEB_DEPLOYMENT_POLICY__: string
declare const __PIXELS_WEB_DEPLOYMENT_TRUST__: string
declare const __PIXELS_WEB_CLIENT_BUILD__: number
declare const __PIXELS_WEB_APPLICATION_NAME__: string
declare const __PIXELS_WEB_ICON_DATA_URL__: string
declare const __PIXELS_WEB_OEM_PROFILE_SHA256__: string

declare module '*.vue' {
  import type { DefineComponent } from 'vue'
  const component: DefineComponent<{}, {}, any>
  export default component
}
