import axios from 'axios'

const CSRF_KEY = 'px_user_csrf'

export const userHttp = axios.create({
  baseURL: '',
  timeout: 30000,
  withCredentials: true,
})

export function setUserCsrf(token: string) {
  if (token) sessionStorage.setItem(CSRF_KEY, token)
  else sessionStorage.removeItem(CSRF_KEY)
}

userHttp.interceptors.request.use((config) => {
  const method = (config.method || 'get').toLowerCase()
  if (!['get', 'head', 'options'].includes(method)) {
    const csrf = sessionStorage.getItem(CSRF_KEY)
    if (csrf) config.headers.set('X-CSRF-Token', csrf)
  }
  return config
})

userHttp.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error?.response?.status === 401) setUserCsrf('')
    return Promise.reject(error)
  },
)
