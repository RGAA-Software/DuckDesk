import type { Device } from '@/entity/device.ts'
import type { WsBaseMsg } from '@/entity/ws_base_msg.ts'

export interface DeviceOnlineStateChanged extends WsBaseMsg {
  device_id: string
  online: boolean
}

export function parseDeviceOnlineStateChanged(message: unknown): DeviceOnlineStateChanged | null {
  if (!message || typeof message !== 'object') return null
  const candidate = message as Partial<DeviceOnlineStateChanged>
  if (candidate.msg_type !== 'device_online_state_changed') return null
  if (typeof candidate.device_id !== 'string' || typeof candidate.online !== 'boolean') return null
  return {
    msg_type: candidate.msg_type,
    device_id: candidate.device_id,
    online: candidate.online,
  }
}

export function applyDeviceOnlineStateChanged(
  devices: Device[],
  event: DeviceOnlineStateChanged,
): boolean {
  const device = devices.find((item) => item.device_id === event.device_id)
  if (!device) return false
  device.online = event.online
  return true
}
