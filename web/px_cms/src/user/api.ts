import { setUserCsrf, userHttp } from './http'

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

function data<T>(response: { data: { data: T } }): T {
  return response.data.data
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
    return data<UserProfile>(await userHttp.get('/api/v1/user/me'))
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

export async function getApps() {
  return data<ApplicationCard[]>(await userHttp.get('/api/v1/user/apps'))
}

export async function getInstances() {
  return data<InstanceView[]>(await userHttp.get('/api/v1/user/instances'))
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

export async function openDevice(deviceId: string) {
  const result = data<{ launch_url: string }>(
    await userHttp.post(`/api/v1/user/devices/${encodeURIComponent(deviceId)}/ticket`, {
      client_nonce: nonce(`device_${deviceId}`),
      requested_permissions: ['view', 'input'],
    }),
  )
  window.location.assign(result.launch_url)
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

export async function openInstance(instance: InstanceView, clientNonce?: string) {
  const result = data<{ launch_url: string }>(
    await userHttp.post(
      `/api/v1/user/instances/${encodeURIComponent(instance.instance_id)}/ticket`,
      {
        client_nonce: clientNonce || nonce(`instance_${instance.instance_id}`),
        requested_permissions: ['view', 'input'],
      },
    ),
  )
  window.location.assign(result.launch_url)
}

export async function stopInstance(instanceId: string) {
  return data<InstanceView>(
    await userHttp.post(`/api/v1/user/instances/${encodeURIComponent(instanceId)}/stop`, {}),
  )
}

export async function updateUserName(username: string) {
  return data<UserProfile>(await userHttp.patch('/api/v1/user/me', { username }))
}

export async function changeUserPassword(current_password: string, new_password: string) {
  const result = data<{ profile: UserProfile; csrf_token: string }>(
    await userHttp.post('/api/v1/user/me/password', { current_password, new_password }),
  )
  setUserCsrf(result.csrf_token)
  return result.profile
}
