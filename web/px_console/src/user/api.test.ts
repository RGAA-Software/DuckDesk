import { describe, expect, it } from 'vitest'
import { prepareLaunchUrl } from './api'

describe('prepareLaunchUrl', () => {
  it('encodes the Render password and routing metadata without a bearer capability', () => {
    const value = prepareLaunchUrl({
      launch_url: 'http://device.local:32004/web/',
      device_id: 'D-1',
      instance_id: '',
      stream_id: 'web-session-1',
      password_hash: '0123456789abcdef0123456789abcdef',
      permissions: ['view'],
      relay_host: '',
      relay_port: 0,
      signal_device_id: 'server_D-1',
    })
    const url = new URL(value)
    const fragment = new URLSearchParams(url.hash.slice(1))

    expect(url.searchParams.get('c')).toBeTruthy()
    expect(url.searchParams.get('stream_id')).toBe('web-session-1')
    expect(fragment.get('perms')).toBe('view')
  })

  it.each([
    [false, 'rtc'],
    [true, 'rtc_direct'],
  ])('encodes the managed RTC route when direct_probe_enabled=%s', (directProbe, expected) => {
    const rtcConfig = {
      revision: 9,
      direct_probe_enabled: directProbe,
      expires_at: 1_900_000_000,
      ice_servers: [
        { id: 'stun-primary', urls: ['stun:turn.example.test:3478'] },
        {
          id: 'turn-primary',
          urls: ['turn:turn.example.test:3478?transport=udp', 'turn:turn.example.test:3478?transport=tcp'],
          username: 'short-lived-user',
          credential: 'short-lived-credential',
        },
      ],
    }
    const value = prepareLaunchUrl({
      launch_url: 'https://render.example.test:32004/web/',
      device_id: 'D-1',
      instance_id: 'instance-1',
      stream_id: 'web-session-1',
      password_hash: '0123456789abcdef0123456789abcdef',
      permissions: ['view', 'input', 'file'],
      relay_host: 'relay.example.test',
      relay_port: 30502,
      rtc_ice_config: rtcConfig,
    })
    const url = new URL(value)
    const fragment = new URLSearchParams(url.hash.slice(1))

    expect(url.searchParams.get('connType')).toBe(expected)
    expect(fragment.get('relay_host')).toBe('relay.example.test')
    expect(fragment.get('relay_port')).toBe('30502')

    const encoded = fragment.get('ice')!
    const padded = encoded.replace(/-/g, '+').replace(/_/g, '/')
      + '='.repeat((4 - encoded.length % 4) % 4)
    const decoded = JSON.parse(atob(padded))
    expect(decoded).toEqual(rtcConfig)
    expect(decoded.ice_servers[1].urls).toContain(
      'turn:turn.example.test:3478?transport=tcp',
    )
  })
})
