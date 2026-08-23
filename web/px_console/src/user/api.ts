import { hasUserCsrf, setUserCsrf, userHttp } from './http'

export interface UserProfile {
  uid: string
  username: string
  avatar_path: string
  created_timestamp: number
  must_change_password: boolean
  groups: Array<{ gid: string; name: string }>
}

export interface DeviceSummary {
  device_id: string
  name: string
  online: boolean
  capabilities: string[]
  last_seen_at: number
}

export interface ApplicationCard {
  app_id: string
  name: string
  access_mode: 'public' | 'acl'
  cover_url: string
  running_instance?: { instance_id: string; state: string; reconnectable: boolean }
  version: number
}

export interface InstanceView {
  instance_id: string
  app_id: string
  app_name: string
  state: string
  created_at: number
  started_at?: number
  stopped_at?: number
  error_code?: string
  reconnectable: boolean
}

export interface ResourceSummary {
  device_count: number
  application_count: number
  active_instance_count: number
}

export interface ResourcePage<T> {
  items: T[]
  page: number
  page_size: number
  total: number
}

export interface TicketLaunch {
  launch_url: string
  renewal_token: string
  permissions: string[]
  rtc_ice_config?: {
    revision: number
    direct_probe_enabled: boolean
    expires_at: number
    ice_servers: Array<{ id: string; urls: string[]; username?: string; credential?: string }>
  }
  relay_host?: string
  relay_port?: number
  signal_device_id?: string
}

function data<T>(response: { data: { data: T } }): T {
  return response.data.data
}

let csrfRefreshPromise: Promise<void> | null = null

async function ensureUserCsrf() {
  if (hasUserCsrf()) return
  if (!csrfRefreshPromise) {
    csrfRefreshPromise = (async () => {
      const result = data<{ csrf_token: string }>(
        await userHttp.get('/api/v1/session/user/csrf'),
      )
      setUserCsrf(result.csrf_token)
    })().finally(() => {
      csrfRefreshPromise = null
    })
  }
  await csrfRefreshPromise
}

export async function loginUser(username: string, password: string) {
  const result = data<{ profile: UserProfile; csrf_token: string }>(
    await userHttp.post('/api/v1/session/user/login', {
      username,
      password,
      client_type: 'user_web',
    }),
  )
  setUserCsrf(result.csrf_token)
  return result.profile
}

export async function queryUser(): Promise<UserProfile | null> {
  try {
    const profile = data<UserProfile>(await userHttp.get('/api/v1/user/me'))
    await ensureUserCsrf()
    return profile
  } catch (error: any) {
    if (error?.response?.status === 401) return null
    throw error
  }
}

export async function logoutUser() {
  await userHttp.post('/api/v1/session/user/logout', {})
  setUserCsrf('')
}

export async function getSummary() {
  return data<ResourceSummary>(await userHttp.get('/api/v1/user/resources/summary'))
}

export async function getDevices() {
  return data<DeviceSummary[]>(await userHttp.get('/api/v1/user/devices'))
}

export async function getDevicesPage(page = 1, pageSize = 12, keyword = '') {
  return data<ResourcePage<DeviceSummary>>(
    await userHttp.get('/api/v1/user/devices/page', {
      params: { page, page_size: pageSize, keyword },
    }),
  )
}

export async function getApps() {
  return data<ApplicationCard[]>(await userHttp.get('/api/v1/user/apps'))
}

export async function getAppsPage(page = 1, pageSize = 12, keyword = '') {
  return data<ResourcePage<ApplicationCard>>(
    await userHttp.get('/api/v1/user/apps/page', {
      params: { page, page_size: pageSize, keyword },
    }),
  )
}

export async function getInstances() {
  return data<InstanceView[]>(await userHttp.get('/api/v1/user/instances'))
}

export async function getInstancesPage(page = 1, pageSize = 10, keyword = '', state = '') {
  return data<ResourcePage<InstanceView>>(
    await userHttp.get('/api/v1/user/instances/page', {
      params: { page, page_size: pageSize, keyword, state },
    }),
  )
}

export async function waitForInstance(instanceId: string, timeoutMs = 30000) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    const instance = (await getInstances()).find((item) => item.instance_id === instanceId)
    if (!instance) throw new Error('实例不存在或已回收')
    if (instance.state === 'running') return instance
    if (instance.state === 'failed' || instance.state === 'stopped') {
      throw new Error(instance.error_code || '实例启动失败')
    }
    await new Promise((resolve) => window.setTimeout(resolve, 800))
  }
  throw new Error('实例启动超时')
}

function nonce(key: string) {
  const storageKey = `px_user_nonce_${key}`
  let value = sessionStorage.getItem(storageKey)
  if (!value) {
    value = crypto.randomUUID()
    sessionStorage.setItem(storageKey, value)
  }
  return value
}

export function prepareLaunchUrl(result: TicketLaunch) {
  const launch = new URL(result.launch_url, window.location.href)
  if (result.rtc_ice_config) {
    launch.searchParams.set(
      'connType',
      result.rtc_ice_config.direct_probe_enabled ? 'rtc_direct' : 'rtc',
    )
  }
  const fragment = new URLSearchParams(launch.hash.replace(/^#/, ''))
  fragment.set(
    'renew_url',
    `${window.location.origin}/api/v1/connection-tickets/renew`,
  )
  if (result.renewal_token) fragment.set('renew', result.renewal_token)
  if (result.permissions?.length) fragment.set('perms', result.permissions.join(','))
  if (result.relay_host) fragment.set('relay_host', result.relay_host)
  if (result.relay_port) fragment.set('relay_port', String(result.relay_port))
  if (result.signal_device_id) fragment.set('signal_device_id', result.signal_device_id)
  if (result.rtc_ice_config) {
    const bytes = new TextEncoder().encode(JSON.stringify(result.rtc_ice_config))
    let binary = ''
    for (const byte of bytes) binary += String.fromCharCode(byte)
    fragment.set('ice', btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, ''))
  }
  launch.hash = fragment.toString()
  return launch.toString()
}

function requestedPermissions(viewOnly: boolean) {
  return viewOnly ? ['view'] : ['view', 'input', 'clipboard', 'file', 'audio']
}

export async function openDevice(deviceId: string, viewOnly = false) {
  const result = data<TicketLaunch>(
    await userHttp.post(`/api/v1/user/devices/${encodeURIComponent(deviceId)}/ticket`, {
      client_nonce: nonce(`device_${deviceId}`),
      requested_permissions: requestedPermissions(viewOnly),
    }),
  )
  window.location.assign(prepareLaunchUrl(result))
}

export async function startApp(appId: string) {
  const clientNonce = nonce(`app_${appId}`)
  const instance = data<InstanceView>(
    await userHttp.post(`/api/v1/user/apps/${encodeURIComponent(appId)}/start`, {
      client_nonce: clientNonce,
    }),
  )
  return { instance, clientNonce }
}

export async function openInstance(instance: InstanceView, clientNonce?: string, viewOnly = false) {
  const result = data<TicketLaunch>(
    await userHttp.post(
      `/api/v1/user/instances/${encodeURIComponent(instance.instance_id)}/ticket`,
      {
        client_nonce: clientNonce || nonce(`instance_${instance.instance_id}`),
        requested_permissions: requestedPermissions(viewOnly),
      },
    ),
  )
  window.location.assign(prepareLaunchUrl(result))
}

export async function stopInstance(instanceId: string) {
  return data<InstanceView>(
    await userHttp.post(`/api/v1/user/instances/${encodeURIComponent(instanceId)}/stop`, {}),
  )
}

export async function updateUserName(username: string) {
  return data<UserProfile>(await userHttp.patch('/api/v1/user/me', { username }))
}

export async function uploadUserAvatar(file: File) {
  const form = new FormData()
  form.append('file', file, file.name)
  return data<UserProfile>(await userHttp.put('/api/v1/user/me/avatar', form))
}

export async function logoutAllUserSessions(current_password: string) {
  await userHttp.post('/api/v1/session/user/logout-all', { current_password })
  setUserCsrf('')
}

export async function changeUserPassword(current_password: string, new_password: string) {
  const result = data<{ profile: UserProfile; csrf_token: string }>(
    await userHttp.post('/api/v1/user/me/password', { current_password, new_password }),
  )
  setUserCsrf(result.csrf_token)
  return result.profile
}
