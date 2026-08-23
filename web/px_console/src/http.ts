import axios from 'axios'

// 获取基础URL
const getBaseURL = () => {
  const { protocol, hostname, port } = window.location

  // 开发模式走 Vite 代理（同源，见 vite.config.ts 的 proxy），保持相对路径即可
  if (import.meta.env.DEV) {
    return ''
  }

  const basePort = port ? `:${port}` : ''
  return `${protocol}//${hostname}${basePort}`
}

const getHostPort = () => {
  const { hostname, port } = window.location

  // 开发模式 WebSocket 也走 Vite 代理（/console 已配置 ws:true）
  if (import.meta.env.DEV) {
    return window.location.host
  }

  const basePort = port ? `:${port}` : ''
  return `${hostname}${basePort}`
}

// 导出 baseURL 常量
export const BASE_URL = getBaseURL()
export const HOST_PORT = getHostPort()

const axiosHttp = axios.create({
  baseURL: getBaseURL(),
  timeout: 5000,
  withCredentials: true,
  headers: { 'X-Custom-Header': 'foobar' },
})

const CSRF_STORAGE_KEY = 'px_admin_csrf'

export function setAdminCsrfToken(token: string) {
  if (token) sessionStorage.setItem(CSRF_STORAGE_KEY, token)
  else sessionStorage.removeItem(CSRF_STORAGE_KEY)
}

export function getAdminCsrfToken(): string {
  return sessionStorage.getItem(CSRF_STORAGE_KEY) || ''
}

axiosHttp.interceptors.request.use((config) => {
  const method = (config.method || 'get').toLowerCase()
  if (!['get', 'head', 'options'].includes(method)) {
    const csrf = getAdminCsrfToken()
    if (csrf) config.headers.set('X-CSRF-Token', csrf)
  }
  return config
})

axiosHttp.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error?.response?.status === 401) setAdminCsrfToken('')
    return Promise.reject(error)
  },
)

export default axiosHttp
