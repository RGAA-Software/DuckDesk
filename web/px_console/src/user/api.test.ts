import { describe, expect, it } from 'vitest'
import { prepareLaunchUrl } from './api'

describe('prepareLaunchUrl', () => {
  it('keeps secrets in the fragment and attaches the rotating renewal endpoint', () => {
    const value = prepareLaunchUrl({
      launch_url: 'http://device.local:32004/web_client/?deviceId=D-1#ticket=t-1&nonce=n-1',
      renewal_token: 'r-1',
      permissions: ['view'],
    })
    const url = new URL(value)
    const fragment = new URLSearchParams(url.hash.slice(1))

    expect(url.searchParams.get('ticket')).toBeNull()
    expect(fragment.get('ticket')).toBe('t-1')
    expect(fragment.get('renew')).toBe('r-1')
    expect(fragment.get('perms')).toBe('view')
    expect(fragment.get('renew_url')).toBe(
      `${window.location.origin}/api/v1/connection-tickets/renew`,
    )
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
      launch_url: 'https://render.example.test:32004/web_client/?deviceId=D-1#ticket=t-1&nonce=n-1',
      renewal_token: 'r-1',
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
    expect(fragment.get('ticket')).toBe('t-1')
    expect(url.searchParams.get('ticket')).toBeNull()

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
