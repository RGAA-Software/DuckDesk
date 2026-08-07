import axiosHttp from '@/http.ts'
import type {
  AppInstance,
  Application,
  AppPlacement,
  CreateApplicationReq,
  CreatePlacementReq,
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

async function unwrapOne<T>(promise: Promise<{ status: number; data: any }>): Promise<T | null> {
  const resp = await promise
  if (resp.status !== 200 || resp.data?.code !== 200) {
    console.error('app api failed', resp)
    return null
  }
  return resp.data.data as T
}

export async function listApplications(): Promise<Application[] | null> {
  return unwrapList<Application>('/api/v1/app/control/app/list')
}

export async function createApplication(req: CreateApplicationReq): Promise<Application | null> {
  return unwrapOne(
    axiosHttp.post('/api/v1/app/control/app/create', req, { params: appkeyParams() }),
  )
}

export async function listPlacements(): Promise<AppPlacement[] | null> {
  return unwrapList<AppPlacement>('/api/v1/app/control/app/placement/list')
}

export async function createPlacement(req: CreatePlacementReq): Promise<AppPlacement | null> {
  return unwrapOne(
    axiosHttp.post('/api/v1/app/control/app/placement/create', req, { params: appkeyParams() }),
  )
}

export async function listInstances(): Promise<AppInstance[] | null> {
  return unwrapList<AppInstance>('/api/v1/app/control/app/instance/list')
}

export async function startInstance(req: StartInstanceReq): Promise<AppInstance | null> {
  return unwrapOne(
    axiosHttp.post('/api/v1/app/control/app/instance/start', req, { params: appkeyParams() }),
  )
}

export async function stopInstance(instanceId: string): Promise<AppInstance | null> {
  return unwrapOne(
    axiosHttp.post(`/api/v1/app/control/app/instance/stop/${encodeURIComponent(instanceId)}`, null, {
      params: appkeyParams(),
    }),
  )
}
