import axios from 'axios'

// 获取基础URL：dev 环境指向本地 gr_desk_server，prod 同源
const getBaseURL = () => {
  const { protocol, hostname, port } = window.location

  if (import.meta.env.DEV) {
    return 'https://127.0.0.1:5001'
  }

  const basePort = port ? `:${port}` : ''
  return `${protocol}//${hostname}${basePort}`
}

const axiosHttp = axios.create({
  baseURL: getBaseURL(),
  timeout: 5000,
})

export default axiosHttp
