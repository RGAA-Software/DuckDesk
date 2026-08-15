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

  // 开发模式 WebSocket 也走 Vite 代理（/cms 已配置 ws:true）
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
  headers: { 'X-Custom-Header': 'foobar' },
})

export default axiosHttp
