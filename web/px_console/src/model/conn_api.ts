// request service/panel connections
import axiosHttp from '@/http.ts'
import type { ServiceConn } from '@/entity/service_conn.ts'
import type { PanelConn } from '@/entity/panel_conn.ts'
import type { RemoteSession, RemoteSessionEvent } from '@/entity/remote_session.ts'

async function readResponse<T>(request: Promise<{ status: number; data: { code: number; data: T } }>, label: string): Promise<T | null> {
  const resp = await request
  if (resp.status !== 200 || resp.data.code !== 200) {
    console.error(`${label} failed`, resp)
    return null
  }
  return resp.data.data
}

// query all service connections
export async function queryAllServiceConn(): Promise<ServiceConn[] | null> {
  return readResponse(axiosHttp.get('/api/v1/service/control/query/all/service/conn'), 'queryAllServiceConn')
}

// query all panel connections
export async function queryAllPanelConn(): Promise<PanelConn[] | null> {
  return readResponse(axiosHttp.get('/api/v1/panel/control/query/all/panel/conn'), 'queryAllPanelConn')
}

export async function queryRemoteSessions(deviceId: string): Promise<RemoteSession[] | null> {
  return readResponse(
    axiosHttp.get('/api/v1/service/control/query/remote/sessions', { params: { device_id: deviceId } }),
    'queryRemoteSessions',
  )
}

export async function queryRemoteSessionEvents(deviceId: string): Promise<RemoteSessionEvent[] | null> {
  return readResponse(
    axiosHttp.get('/api/v1/service/control/query/remote/session/events', { params: { device_id: deviceId } }),
    'queryRemoteSessionEvents',
  )
}
