// request device
import axiosHttp from '@/http.ts'
import type { Device } from '@/entity/device.ts'

export async function queryDevices(
  deviceName: string,
  deviceId: string,
  ip: string,
  state: string,
  page: number,
  pageSize: number,
) {
  const resp = await axiosHttp.get('/api/v1/device/control/query/devices', {
    params: {
      device_name: deviceName.trim(),
      device_id: deviceId.trim(),
      ip: ip.trim(),
      online_state: state ? state.trim() : '',
      page: page,
      page_size: pageSize,
      appkey: localStorage.getItem('appkey'),
    },
  })
  if (resp.status !== 200) {
    console.error('queryDevices failed', resp)
    return null
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryDevices failed, data:', data)
    return null
  }
  console.log('devices: ', data.data)
  return data.data
}


// update device active state
export async function updateDeviceActive(device: Device, active: boolean) {
  const resp = await axiosHttp.post(
    '/api/v1/device/control/update/device/active?appkey=' + localStorage.getItem('appkey'),
    {
      device_id: device.device_id,
      active: active,
    },
  )
  if (resp.status !== 200) {
    console.error('queryDevices failed', resp)
    return false
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryDevices failed, data:', data)
    return false
  }

  return true
}

