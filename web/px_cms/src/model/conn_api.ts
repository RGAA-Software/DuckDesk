// request service/panel connections
import axiosHttp from '@/http.ts'
import type { ServiceConn } from '@/entity/service_conn.ts'
import type { PanelConn } from '@/entity/panel_conn.ts'

// query all service connections
export async function queryAllServiceConn(): Promise<ServiceConn[] | null> {
  const resp = await axiosHttp.get('/api/v1/service/control/query/all/service/conn', {
    params: {
      appkey: localStorage.getItem('appkey'),
    },
  })
  if (resp.status !== 200) {
    console.error('queryAllServiceConn failed', resp)
    return null
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryAllServiceConn failed, data:', data)
    return null
  }
  console.log('service conns: ', data.data)
  return data.data
}

// query all panel connections
export async function queryAllPanelConn(): Promise<PanelConn[] | null> {
  const resp = await axiosHttp.get('/api/v1/panel/control/query/all/panel/conn', {
    params: {
      appkey: localStorage.getItem('appkey'),
    },
  })
  if (resp.status !== 200) {
    console.error('queryAllPanelConn failed', resp)
    return null
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryAllPanelConn failed, data:', data)
    return null
  }
  console.log('panel conns: ', data.data)
  return data.data
}
