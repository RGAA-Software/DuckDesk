import axiosHttp from '@/http.ts'
import type {
  AppInstance,
  AppNode,
  AppRow,
  SaveAppReq,
  SaveNodeReq,
  StartInstanceReq,
} from '@/entity/app_schedule.ts'

function appkeyParams() {
  return { appkey: localStorage.getItem('appkey') }
}

async function unwrapList<T>(path: string): Promise<T[] | null> {
  const resp = await axiosHttp.get(path, { params: appkeyParams() })
  if (resp.status !== 200 || resp.data?.code !== 200) {
    console.error(path, 'failed', resp)
    return null
  }
  return resp.data.data as T[]
}

export async function listAppRows(): Promise<AppRow[] | null> {
  return unwrapList<AppRow>('/api/v1/app/control/app/rows')
}

export async function nextPort(deviceId: string): Promise<number | null> {
  const resp = await axiosHttp.get('/api/v1/app/control/app/next-port', {
    params: { ...appkeyParams(), device_id: deviceId },
  })
  if (resp.status !== 200 || resp.data?.code !== 200) return null
  return resp.data.data as number
}

export async function saveApp(req: SaveAppReq): Promise<{ ok: true; data: AppRow } | { ok: false; message: string }> {
  const resp = await axiosHttp.post('/api/v1/app/control/app/save', req, { params: appkeyParams() })
  if (resp.status !== 200) {
    return { ok: false, message: '网络错误' }
  }
  if (resp.data?.code !== 200) {
    return { ok: false, message: resp.data?.message || '保存失败' }
  }
  return { ok: true, data: resp.data.data as AppRow }
}

export async function deleteApp(appId: string): Promise<{ ok: true } | { ok: false; message: string }> {
  const resp = await axiosHttp.post(
    `/api/v1/app/control/app/delete/${encodeURIComponent(appId)}`,
    null,
    { params: appkeyParams() },
  )
  if (resp.status !== 200) return { ok: false, message: '网络错误' }
  if (resp.data?.code !== 200) {
    return { ok: false, message: resp.data?.message || '删除失败' }
  }
  return { ok: true }
}

export async function saveNode(
  req: SaveNodeReq,
): Promise<{ ok: true; data: AppNode } | { ok: false; message: string }> {
  const resp = await axiosHttp.post('/api/v1/app/control/app/node/save', req, {
    params: appkeyParams(),
  })
  if (resp.status !== 200) return { ok: false, message: '网络错误' }
  if (resp.data?.code !== 200) {
    return { ok: false, message: resp.data?.message || '保存节失败' }
  }
  return { ok: true, data: resp.data.data as AppNode }
}

export async function deleteNode(nodeId: string): Promise<{ ok: true } | { ok: false; message: string }> {
  const resp = await axiosHttp.post(
    `/api/v1/app/control/app/node/delete/${encodeURIComponent(nodeId)}`,
    null,
    { params: appkeyParams() },
  )
  if (resp.status !== 200) return { ok: false, message: '网络错误' }
  if (resp.data?.code !== 200) {
    return { ok: false, message: resp.data?.message || '删除节失败' }
  }
  return { ok: true }
}

export async function startNode(
  nodeId: string,
): Promise<{ ok: true; data: AppInstance } | { ok: false; message: string }> {
  const resp = await axiosHttp.post(
    `/api/v1/app/control/app/node/start/${encodeURIComponent(nodeId)}`,
    null,
    { params: appkeyParams() },
  )
  if (resp.status !== 200) return { ok: false, message: '网络错误' }
  if (resp.data?.code !== 200) {
    return { ok: false, message: resp.data?.message || '启动失败' }
  }
  return { ok: true, data: resp.data.data as AppInstance }
}

export async function listInstances(): Promise<AppInstance[] | null> {
  return unwrapList<AppInstance>('/api/v1/app/control/app/instance/list')
}

export async function startInstance(
  req: StartInstanceReq,
): Promise<{ ok: true; data: AppInstance } | { ok: false; message: string }> {
  const resp = await axiosHttp.post('/api/v1/app/control/app/instance/start', req, {
    params: appkeyParams(),
  })
  if (resp.status !== 200) return { ok: false, message: '网络错误' }
  if (resp.data?.code !== 200) {
    return { ok: false, message: resp.data?.message || '启动失败' }
  }
  return { ok: true, data: resp.data.data as AppInstance }
}

export async function stopInstance(
  instanceId: string,
): Promise<{ ok: true; data: AppInstance } | { ok: false; message: string }> {
  const resp = await axiosHttp.post(
    `/api/v1/app/control/app/instance/stop/${encodeURIComponent(instanceId)}`,
    null,
    { params: appkeyParams() },
  )
  if (resp.status !== 200) return { ok: false, message: '网络错误' }
  if (resp.data?.code !== 200) {
    return { ok: false, message: resp.data?.message || '停止失败' }
  }
  return { ok: true, data: resp.data.data as AppInstance }
}
