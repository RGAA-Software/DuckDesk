import axios from 'axios'
import { getBaseURL } from '@/http.ts'

const TOKEN_KEY = 'godesk_admin_token'

export const getAdminToken = () => sessionStorage.getItem(TOKEN_KEY) || ''
export const setAdminToken = (t: string) => sessionStorage.setItem(TOKEN_KEY, t)
export const clearAdminToken = () => sessionStorage.removeItem(TOKEN_KEY)

const adminHttp = axios.create({
  // dev 走 vite 代理（同源，避免 CORS）；prod 同源
  baseURL: import.meta.env.DEV ? '' : getBaseURL(),
  timeout: 8000,
})

adminHttp.interceptors.request.use((config) => {
  config.headers['X-Admin-Token'] = getAdminToken()
  return config
})

adminHttp.interceptors.response.use(
  (resp) => resp,
  (error) => {
    const status = error?.response?.status
    if (status === 401 || status === 606) {
      clearAdminToken()
      if (window.location.pathname !== '/admin') {
        window.location.href = '/admin'
      }
    }
    return Promise.reject(error)
  },
)

export default adminHttp
