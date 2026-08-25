import { describe, expect, it } from 'vitest'
import { Device } from '@/entity/device.ts'
import {
  applyDeviceOnlineStateChanged,
  parseDeviceOnlineStateChanged,
} from '@/model/device_online_state.ts'

describe('device online WebSocket state', () => {
  it('updates only the matching device', () => {
    const first = new Device()
    first.device_id = 'device-1'
    const second = new Device()
    second.device_id = 'device-2'

    const event = parseDeviceOnlineStateChanged({
      msg_type: 'device_online_state_changed',
      device_id: 'device-1',
      online: true,
    })

    expect(event).not.toBeNull()
    expect(applyDeviceOnlineStateChanged([first, second], event!)).toBe(true)
    expect(first.online).toBe(true)
    expect(second.online).toBe(false)
  })

  it('ignores malformed and unrelated messages', () => {
    expect(parseDeviceOnlineStateChanged({ msg_type: 'heartbeat' })).toBeNull()
    expect(
      parseDeviceOnlineStateChanged({
        msg_type: 'device_online_state_changed',
        device_id: 'device-1',
        online: 'yes',
      }),
    ).toBeNull()
  })
})
