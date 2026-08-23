import axios from 'axios'

const CSRF_KEY = 'px_user_csrf'
export const USER_SESSION_EXPIRED_EVENT = 'px-user-session-expired'

export const userHttp = axios.create({
  baseURL: '',
  timeout: 30000,
  withCredentials: true,
})

export function setUserCsrf(token: string) {
  if (token) localStorage.setItem(CSRF_KEY, token)
  else localStorage.removeItem(CSRF_KEY)
  // Remove values created by older builds so there is only one source of truth.
  sessionStorage.removeItem(CSRF_KEY)
}

export function hasUserCsrf() {
  return Boolean(localStorage.getItem(CSRF_KEY))
}

userHttp.interceptors.request.use((config) => {
  const method = (config.method || 'get').toLowerCase()
  if (!['get', 'head', 'options'].includes(method)) {
    const csrf = localStorage.getItem(CSRF_KEY)
    if (csrf) config.headers.set('X-CSRF-Token', csrf)
  }
  return config
})

userHttp.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error?.response?.status === 401) {
      setUserCsrf('')
      // Polling can discover expiry while the router is idle. Notify the user
      // layout so stale private resources disappear immediately; initial
      // navigation remains the responsibility of the router guard.
      window.dispatchEvent(new Event(USER_SESSION_EXPIRED_EVENT))
    }
    return Promise.reject(error)
  },
)
