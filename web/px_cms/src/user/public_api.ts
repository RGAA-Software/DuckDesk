import axios from 'axios'
import { prepareLaunchUrl, type ApplicationCard, type InstanceView, type TicketLaunch } from './api'

const CSRF_KEY = 'px_guest_csrf'
const guestHttp = axios.create({ baseURL: '', timeout: 15000, withCredentials: true })
guestHttp.interceptors.request.use((config) => {
  const method = (config.method || 'get').toLowerCase()
  if (!['get', 'head', 'options'].includes(method)) {
    const csrf = sessionStorage.getItem(CSRF_KEY)
    if (csrf) config.headers.set('X-CSRF-Token', csrf)
  }
  return config
})

const unwrap = <T>(response: { data: { data: T } }) => response.data.data
const nonce = (key: string) => {
  const storageKey = `px_guest_nonce_${key}`
  let value = sessionStorage.getItem(storageKey)
  if (!value) {
    value = crypto.randomUUID()
    sessionStorage.setItem(storageKey, value)
  }
  return value
}

export async function ensureGuestSession(force = false) {
  if (!force && sessionStorage.getItem(CSRF_KEY)) return
  if (force) sessionStorage.removeItem(CSRF_KEY)
  const result = unwrap<{ csrf_token: string }>(
    await guestHttp.post('/api/v1/session/guest', { client_nonce: crypto.randomUUID() }),
  )
  sessionStorage.setItem(CSRF_KEY, result.csrf_token)
}

export async function registerUser(username: string, password: string) {
  await ensureGuestSession()
  return unwrap<{ uid: string; username: string }>(
    await guestHttp.post('/api/v1/user/register', {
      username,
      password,
    }),
  )
}

export async function getPublicApps() {
  return unwrap<ApplicationCard[]>(await guestHttp.get('/api/v1/public/apps'))
}

export async function startPublicApp(appId: string) {
  await ensureGuestSession()
  const clientNonce = nonce(`app_${appId}`)
  const request = () => guestHttp.post(`/api/v1/public/apps/${encodeURIComponent(appId)}/start`, {
    client_nonce: clientNonce,
  })
  let response
  try {
    response = await request()
  } catch (error: any) {
    if (error?.response?.status !== 401) throw error
    // sessionStorage can outlive the server-side guest session after CMS is
    // restarted or its test database is reset. Recreate the HttpOnly cookie
    // and CSRF pair once, then repeat the idempotent start request.
    await ensureGuestSession(true)
    response = await request()
  }
  const instance = unwrap<InstanceView>(response)
  return { instance, clientNonce }
}

export async function waitForGuestInstance(instanceId: string, timeoutMs = 30000) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    const instances = unwrap<InstanceView[]>(await guestHttp.get('/api/v1/public/instances'))
    const instance = instances.find((item) => item.instance_id === instanceId)
    if (!instance) throw new Error('实例不存在或已回收')
    if (instance.state === 'running') return instance
    if (instance.state === 'failed' || instance.state === 'stopped') {
      throw new Error(instance.error_code || '实例启动失败')
    }
    await new Promise((resolve) => window.setTimeout(resolve, 800))
  }
  throw new Error('实例启动超时')
}

export async function getGuestInstances() {
  await ensureGuestSession()
  try {
    return unwrap<InstanceView[]>(await guestHttp.get('/api/v1/public/instances'))
  } catch (error: any) {
    if (error?.response?.status !== 401) throw error
    await ensureGuestSession(true)
    return unwrap<InstanceView[]>(await guestHttp.get('/api/v1/public/instances'))
  }
}

export async function openGuestInstance(instance: InstanceView, clientNonce: string, viewOnly = false) {
  const result = unwrap<TicketLaunch>(
    await guestHttp.post(
      `/api/v1/public/instances/${encodeURIComponent(instance.instance_id)}/ticket`,
      { client_nonce: clientNonce, requested_permissions: viewOnly ? ['view'] : ['view', 'input'] },
    ),
  )
  window.location.assign(prepareLaunchUrl(result))
}

export async function stopGuestInstance(instanceId: string) {
  return unwrap<InstanceView>(
    await guestHttp.post(
      `/api/v1/public/instances/${encodeURIComponent(instanceId)}/stop`,
      { reason: 'guest_requested' },
    ),
  )
}
