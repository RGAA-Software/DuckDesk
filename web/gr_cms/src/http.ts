import axios from 'axios'

// 获取基础URL
const getBaseURL = () => {
  const { protocol, hostname, port } = window.location

  if (import.meta.env.DEV) {
    return 'http://127.0.0.1:30499'
  }

  const basePort = port ? `:${port}` : ''
  return `${protocol}//${hostname}${basePort}`
}

const getHostPort = () => {
  const { hostname, port } = window.location

  if (import.meta.env.DEV) {
    return '127.0.0.1:30499'
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
